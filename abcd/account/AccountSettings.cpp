/*
 * Copyright (c) 2014, AirBitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */

#include "AccountSettings.hpp"
#include "../crypto/Crypto.hpp"
#include "../exchange/Exchange.hpp"
#include "../login/Login.hpp"
#include "../util/FileIO.hpp"
#include "../util/Json.hpp"
#include "../util/Mutex.hpp"
#include "../util/Util.hpp"

namespace abcd {

#define ACCOUNT_SETTINGS_FILENAME               "Settings.json"

// Settings JSON fields:
#define JSON_ACCT_FIRST_NAME_FIELD              "firstName"
#define JSON_ACCT_LAST_NAME_FIELD               "lastName"
#define JSON_ACCT_NICKNAME_FIELD                "nickname"
#define JSON_ACCT_PIN_FIELD                     "PIN"
#define JSON_ACCT_NAME_ON_PAYMENTS_FIELD        "nameOnPayments"
#define JSON_ACCT_MINUTES_AUTO_LOGOUT_FIELD     "minutesAutoLogout"
#define JSON_ACCT_RECOVERY_REMINDER_COUNT       "recoveryReminderCount"
#define JSON_ACCT_LANGUAGE_FIELD                "language"
#define JSON_ACCT_NUM_CURRENCY_FIELD            "numCurrency"
#define JSON_ACCT_EX_RATE_SOURCES_FIELD         "exchangeRateSources"
#define JSON_ACCT_EX_RATE_SOURCE_FIELD          "exchangeRateSource"
#define JSON_ACCT_BITCOIN_DENOMINATION_FIELD    "bitcoinDenomination"
#define JSON_ACCT_LABEL_FIELD                   "label"
#define JSON_ACCT_LABEL_TYPE                    "labeltype"
#define JSON_ACCT_SATOSHI_FIELD                 "satoshi"
#define JSON_ACCT_ADVANCED_FEATURES_FIELD       "advancedFeatures"
#define JSON_ACCT_DAILY_SPEND_LIMIT_ENABLED     "dailySpendLimitEnabled"
#define JSON_ACCT_DAILY_SPEND_LIMIT_SATOSHIS    "dailySpendLimitSatoshis"
#define JSON_ACCT_SPEND_REQUIRE_PIN_ENABLED     "spendRequirePinEnabled"
#define JSON_ACCT_SPEND_REQUIRE_PIN_SATOSHIS    "spendRequirePinSatoshis"
#define JSON_ACCT_DISABLE_PIN_LOGIN             "disablePINLogin"

#define DEF_REQUIRE_PIN_SATOSHIS 5000000

static tABC_CC ABC_AccountSettingsCreateDefault(tABC_AccountSettings **ppSettings, tABC_Error *pError);

/**
 * Creates default account settings
 *
 * @param ppSettings    Location to store ptr to allocated settings (caller must free)
 * @param pError        A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_AccountSettingsCreateDefault(tABC_AccountSettings **ppSettings,
                                         tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);
    tABC_AccountSettings *pSettings = NULL;
    tABC_ExchangeRateSource **aSources = NULL;
    unsigned i = 0;

    ABC_CHECK_NULL(ppSettings);

    ABC_NEW(pSettings, tABC_AccountSettings);

    pSettings->szFirstName = NULL;
    pSettings->szLastName = NULL;
    pSettings->szNickname = NULL;
    pSettings->bNameOnPayments = false;
    pSettings->minutesAutoLogout = 60;
    pSettings->recoveryReminderCount = 0;

    pSettings->bDailySpendLimit = false;
    pSettings->bSpendRequirePin = true;
    pSettings->spendRequirePinSatoshis = DEF_REQUIRE_PIN_SATOSHIS;
    pSettings->bDisablePINLogin = false;

    ABC_STRDUP(pSettings->szLanguage, "en");
    pSettings->currencyNum = CURRENCY_NUM_USD;

    pSettings->exchangeRateSources.numSources = EXCHANGE_DEFAULTS_SIZE;
    ABC_ARRAY_NEW(pSettings->exchangeRateSources.aSources,
                    pSettings->exchangeRateSources.numSources, tABC_ExchangeRateSource*);

    aSources = pSettings->exchangeRateSources.aSources;

    for (i = 0; i < EXCHANGE_DEFAULTS_SIZE; ++i)
    {
        ABC_NEW(aSources[i], tABC_ExchangeRateSource);
        aSources[i]->currencyNum = EXCHANGE_DEFAULTS[i].currencyNum;
        ABC_STRDUP(aSources[i]->szSource, EXCHANGE_DEFAULTS[i].szDefaultExchange);
    }

    pSettings->bitcoinDenomination.denominationType = ABC_DENOMINATION_MBTC;
    pSettings->bitcoinDenomination.satoshi = 100000;

    // assign final settings
    *ppSettings = pSettings;
    pSettings = NULL;

exit:
    ABC_AccountSettingsFree(pSettings);

    return cc;
}

/**
 * Loads the settings for a specific account using the given key
 * If no settings file exists for the given user, defaults are created
 *
 * @param login         Access to the account sync dir
 * @param ppSettings    Location to store ptr to allocated settings (caller must free)
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_AccountSettingsLoad(const Login &login,
                                tABC_AccountSettings **ppSettings,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    AutoCoreLock lock(gCoreMutex);

    tABC_AccountSettings *pSettings = NULL;
    json_t *pJSON_Root = NULL;
    json_t *pJSON_Value = NULL;
    bool bExists = false;
    auto filename = login.syncDir() + ACCOUNT_SETTINGS_FILENAME;

    ABC_CHECK_NULL(ppSettings);

    ABC_CHECK_RET(ABC_FileIOFileExists(filename.c_str(), &bExists, pError));
    if (true == bExists)
    {
        // load and decrypted the file into a json object
        ABC_CHECK_RET(ABC_CryptoDecryptJSONFileObject(filename.c_str(),
            toU08Buf(login.dataKey()), &pJSON_Root, pError));
        //ABC_DebugLog("Loaded settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));

        // allocate the new settings object
        ABC_NEW(pSettings, tABC_AccountSettings);
        pSettings->szFirstName = NULL;
        pSettings->szLastName = NULL;
        pSettings->szNickname = NULL;

        // get the first name
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_FIRST_NAME_FIELD);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT(json_is_string(pJSON_Value), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pSettings->szFirstName, json_string_value(pJSON_Value));
        }

        // get the last name
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_LAST_NAME_FIELD);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT(json_is_string(pJSON_Value), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pSettings->szLastName, json_string_value(pJSON_Value));
        }

        // get the nickname
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_NICKNAME_FIELD);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT(json_is_string(pJSON_Value), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pSettings->szNickname, json_string_value(pJSON_Value));
        }

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_PIN_FIELD);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT(json_is_string(pJSON_Value), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pSettings->szPIN, json_string_value(pJSON_Value));
        }

        // get name on payments option
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_NAME_ON_PAYMENTS_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_boolean(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON boolean value");
        pSettings->bNameOnPayments = json_is_true(pJSON_Value) ? true : false;

        // get minutes auto logout
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_MINUTES_AUTO_LOGOUT_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pSettings->minutesAutoLogout = (int) json_integer_value(pJSON_Value);

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_RECOVERY_REMINDER_COUNT);
        if (pJSON_Value) {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
            pSettings->recoveryReminderCount = (int) json_integer_value(pJSON_Value);
        } else {
            pSettings->recoveryReminderCount = 0;
        }

        // get language
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_LANGUAGE_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
        ABC_STRDUP(pSettings->szLanguage, json_string_value(pJSON_Value));

        // get currency num
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_NUM_CURRENCY_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pSettings->currencyNum = (int) json_integer_value(pJSON_Value);

        // get advanced features
        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_ADVANCED_FEATURES_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_boolean(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON boolean value");
        pSettings->bAdvancedFeatures = json_is_true(pJSON_Value) ? true : false;

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_DAILY_SPEND_LIMIT_ENABLED);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_boolean(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON boolean value");
            pSettings->bDailySpendLimit = json_is_true(pJSON_Value) ? true : false;
        }
        else
        {
            pSettings->bDailySpendLimit = false;
        }

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_DAILY_SPEND_LIMIT_SATOSHIS);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON daily spend satoshi value");
            pSettings->dailySpendLimitSatoshis = (int64_t) json_integer_value(pJSON_Value);
        }
        else
        {
            pSettings->dailySpendLimitSatoshis = 0;
        }

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_SPEND_REQUIRE_PIN_ENABLED);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_boolean(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON boolean value");
            pSettings->bSpendRequirePin = json_is_true(pJSON_Value) ? true : false;
        }
        else
        {
            // Default to PIN required
            pSettings->bSpendRequirePin = true;
        }

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_DISABLE_PIN_LOGIN);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_boolean(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON boolean value");
            pSettings->bDisablePINLogin = json_is_true(pJSON_Value) ? true : false;
        }
        else
        {
            // Default to PIN login allowed
            pSettings->bDisablePINLogin = false;
        }

        pJSON_Value = json_object_get(pJSON_Root, JSON_ACCT_SPEND_REQUIRE_PIN_SATOSHIS);
        if (pJSON_Value)
        {
            ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON daily spend satoshi value");
            pSettings->spendRequirePinSatoshis = (int64_t) json_integer_value(pJSON_Value);
        }
        else
        {
            // Default PIN requirement to 50mb
            pSettings->spendRequirePinSatoshis = DEF_REQUIRE_PIN_SATOSHIS;
        }


        // get the denomination object
        json_t *pJSON_Denom = json_object_get(pJSON_Root, JSON_ACCT_BITCOIN_DENOMINATION_FIELD);
        ABC_CHECK_ASSERT((pJSON_Denom && json_is_object(pJSON_Denom)), ABC_CC_JSONError, "Error parsing JSON object value");

        // get denomination satoshi display size (e.g., 100,000 would be milli-bit coin)
        pJSON_Value = json_object_get(pJSON_Denom, JSON_ACCT_SATOSHI_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pSettings->bitcoinDenomination.satoshi = json_integer_value(pJSON_Value);

        // get denomination type
        pJSON_Value = json_object_get(pJSON_Denom, JSON_ACCT_LABEL_TYPE);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pSettings->bitcoinDenomination.denominationType = json_integer_value(pJSON_Value);

        // get the exchange rates array
        json_t *pJSON_Sources = json_object_get(pJSON_Root, JSON_ACCT_EX_RATE_SOURCES_FIELD);
        ABC_CHECK_ASSERT((pJSON_Sources && json_is_array(pJSON_Sources)), ABC_CC_JSONError, "Error parsing JSON array value");

        // get the number of elements in the array
        pSettings->exchangeRateSources.numSources = (int) json_array_size(pJSON_Sources);
        if (pSettings->exchangeRateSources.numSources > 0)
        {
            ABC_ARRAY_NEW(pSettings->exchangeRateSources.aSources,
                          pSettings->exchangeRateSources.numSources, tABC_ExchangeRateSource*);
        }

        // run through all the sources
        for (unsigned i = 0; i < pSettings->exchangeRateSources.numSources; i++)
        {
            tABC_ExchangeRateSource *pSource = NULL;
            ABC_NEW(pSource, tABC_ExchangeRateSource);

            // get the source object
            json_t *pJSON_Source = json_array_get(pJSON_Sources, i);
            ABC_CHECK_ASSERT((pJSON_Source && json_is_object(pJSON_Source)), ABC_CC_JSONError, "Error parsing JSON array element object");

            // get the currency num
            pJSON_Value = json_object_get(pJSON_Source, JSON_ACCT_NUM_CURRENCY_FIELD);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
            pSource->currencyNum = (int) json_integer_value(pJSON_Value);

            // get the exchange rate source
            pJSON_Value = json_object_get(pJSON_Source, JSON_ACCT_EX_RATE_SOURCE_FIELD);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pSource->szSource, json_string_value(pJSON_Value));

            // assign this source to the array
            pSettings->exchangeRateSources.aSources[i] = pSource;
        }
        // If the user doesn't have defaults for all the exchange rates
        if (pSettings->exchangeRateSources.numSources != EXCHANGE_DEFAULTS_SIZE)
        {
            size_t curExchanges = pSettings->exchangeRateSources.numSources;
            // resize exchange rate array
            ABC_ARRAY_RESIZE(pSettings->exchangeRateSources.aSources,
                             EXCHANGE_DEFAULTS_SIZE, tABC_ExchangeRateSource*);
            for (unsigned i = 0; i < EXCHANGE_DEFAULTS_SIZE; ++i)
            {
                bool found = false;
                for (unsigned j = 0; j < pSettings->exchangeRateSources.numSources; ++j)
                {
                    if (pSettings->exchangeRateSources.aSources[j]->currencyNum
                            == EXCHANGE_DEFAULTS[i].currencyNum)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    tABC_ExchangeRateSource *pSource = NULL;
                    ABC_NEW(pSource, tABC_ExchangeRateSource);
                    pSource->currencyNum = EXCHANGE_DEFAULTS[i].currencyNum;
                    ABC_STRDUP(pSource->szSource, EXCHANGE_DEFAULTS[i].szDefaultExchange);

                    pSettings->exchangeRateSources.aSources[curExchanges] = pSource;
                    curExchanges++;
                }
            }
            pSettings->exchangeRateSources.numSources = curExchanges;
        }

        //
        // Create the user's "fullName" based on First, Last, Nick names
        // Should probably be pulled out into its own function
        //
        {
            size_t f, l, n;
            char *fn, *ln, *nn;

            f = ABC_STRLEN(pSettings->szFirstName);
            l = ABC_STRLEN(pSettings->szLastName);
            n = ABC_STRLEN(pSettings->szNickname);

            fn = pSettings->szFirstName;
            ln = pSettings->szLastName;
            nn = pSettings->szNickname;

            if (f || l || n)
            {
                size_t bufLength = 5 + f + l + n;
                char *fullName;

                if (ABC_STRLEN(pSettings->szFullName) < bufLength)
                {
                    ABC_ARRAY_RESIZE(pSettings->szFullName, bufLength, char);
                }
                fullName = pSettings->szFullName;

                fullName[0] = 0;

                if (f)
                {
                    sprintf(fullName, "%s", fn);
                }
                if (f && l)
                {
                    sprintf(fullName, "%s ", fullName);
                }
                if (l)
                {
                    sprintf(fullName, "%s%s", fullName, ln);
                }
                if ((f || l) && n)
                {
                    sprintf(fullName, "%s - ", fullName);
                }
                if (n)
                {
                    sprintf(fullName, "%s%s", fullName, nn);
                }
            }
        }

    }
    else
    {
        // create the defaults
        ABC_CHECK_RET(ABC_AccountSettingsCreateDefault(&pSettings, pError));
    }

    // assign final settings
    *ppSettings = pSettings;
    pSettings = NULL;

 //   ABC_DebugLog("Loading settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));

exit:
    ABC_AccountSettingsFree(pSettings);
    if (pJSON_Root) json_decref(pJSON_Root);

    return cc;
}

/**
 * Saves the settings for a specific account using the given key
 *
 * @param login         Access to the account sync dir
 * @param pSettings     Pointer to settings
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_AccountSettingsSave(const Login &login,
                                tABC_AccountSettings *pSettings,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    AutoCoreLock lock(gCoreMutex);

    json_t *pJSON_Root = NULL;
    json_t *pJSON_Denom = NULL;
    json_t *pJSON_SourcesArray = NULL;
    json_t *pJSON_Source = NULL;
    int retVal = 0;
    auto filename = login.syncDir() + ACCOUNT_SETTINGS_FILENAME;

    ABC_CHECK_NULL(pSettings);

    if (pSettings->szPIN)
    {
        char *endstr = NULL;
        strtol(pSettings->szPIN, &endstr, 10);
        ABC_CHECK_ASSERT(*endstr == '\0', ABC_CC_NonNumericPin, "The pin must be numeric.");
    }

    // create the json for the settings
    pJSON_Root = json_object();
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_Error, "Could not create settings JSON object");

    // set the first name
    if (pSettings->szFirstName)
    {
        retVal = json_object_set_new(pJSON_Root, JSON_ACCT_FIRST_NAME_FIELD, json_string(pSettings->szFirstName));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");
    }

    // set the last name
    if (pSettings->szLastName)
    {
        retVal = json_object_set_new(pJSON_Root, JSON_ACCT_LAST_NAME_FIELD, json_string(pSettings->szLastName));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");
    }

    // set the nickname
    if (pSettings->szNickname)
    {
        retVal = json_object_set_new(pJSON_Root, JSON_ACCT_NICKNAME_FIELD, json_string(pSettings->szNickname));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");
    }

    // set the pin
    if (pSettings->szPIN)
    {
        retVal = json_object_set_new(pJSON_Root, JSON_ACCT_PIN_FIELD, json_string(pSettings->szPIN));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");
    }

    // set name on payments option
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_NAME_ON_PAYMENTS_FIELD, json_boolean(pSettings->bNameOnPayments));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set minutes auto logout
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_MINUTES_AUTO_LOGOUT_FIELD, json_integer(pSettings->minutesAutoLogout));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set recovery reminder count
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_RECOVERY_REMINDER_COUNT, json_integer(pSettings->recoveryReminderCount));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set language
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_LANGUAGE_FIELD, json_string(pSettings->szLanguage));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set currency num
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_NUM_CURRENCY_FIELD, json_integer(pSettings->currencyNum));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set advanced features
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_ADVANCED_FEATURES_FIELD, json_boolean(pSettings->bAdvancedFeatures));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_DAILY_SPEND_LIMIT_ENABLED, json_boolean(pSettings->bDailySpendLimit));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_DAILY_SPEND_LIMIT_SATOSHIS, json_integer(pSettings->dailySpendLimitSatoshis));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_SPEND_REQUIRE_PIN_ENABLED, json_boolean(pSettings->bSpendRequirePin));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_SPEND_REQUIRE_PIN_SATOSHIS, json_integer(pSettings->spendRequirePinSatoshis));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_DISABLE_PIN_LOGIN, json_boolean(pSettings->bDisablePINLogin));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // create the denomination section
    pJSON_Denom = json_object();
    ABC_CHECK_ASSERT(pJSON_Denom != NULL, ABC_CC_Error, "Could not create settings JSON object");

    // set denomination satoshi display size (e.g., 100,000 would be milli-bit coin)
    retVal = json_object_set_new(pJSON_Denom, JSON_ACCT_SATOSHI_FIELD, json_integer(pSettings->bitcoinDenomination.satoshi));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set denomination type
    retVal = json_object_set_new(pJSON_Denom, JSON_ACCT_LABEL_TYPE, json_integer(pSettings->bitcoinDenomination.denominationType));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // add the denomination object
    retVal = json_object_set(pJSON_Root, JSON_ACCT_BITCOIN_DENOMINATION_FIELD, pJSON_Denom);
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // create the exchange sources array
    pJSON_SourcesArray = json_array();
    ABC_CHECK_ASSERT(pJSON_SourcesArray != NULL, ABC_CC_Error, "Could not create settings JSON object");

    // add the exchange sources
    for (unsigned i = 0; i < pSettings->exchangeRateSources.numSources; i++)
    {
        tABC_ExchangeRateSource *pSource = pSettings->exchangeRateSources.aSources[i];

        // create the source object
        pJSON_Source = json_object();
        ABC_CHECK_ASSERT(pJSON_Source != NULL, ABC_CC_Error, "Could not create settings JSON object");

        // set the currency num
        retVal = json_object_set_new(pJSON_Source, JSON_ACCT_NUM_CURRENCY_FIELD, json_integer(pSource->currencyNum));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

        // set the exchange rate source
        retVal = json_object_set_new(pJSON_Source, JSON_ACCT_EX_RATE_SOURCE_FIELD, json_string(pSource->szSource));
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

        // append this object to our array
        retVal = json_array_append(pJSON_SourcesArray, pJSON_Source);
        ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

        // free the source object
        if (pJSON_Source) json_decref(pJSON_Source);
        pJSON_Source = NULL;
    }

    // add the exchange sources array object
    retVal = json_object_set(pJSON_Root, JSON_ACCT_EX_RATE_SOURCES_FIELD, pJSON_SourcesArray);
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // encrypt and save json
//    ABC_DebugLog("Saving settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));
    ABC_CHECK_RET(ABC_CryptoEncryptJSONFileObject(pJSON_Root,
        toU08Buf(login.dataKey()), ABC_CryptoType_AES256,
        filename.c_str(), pError));


exit:
    if (pJSON_Root) json_decref(pJSON_Root);
    if (pJSON_Denom) json_decref(pJSON_Denom);
    if (pJSON_SourcesArray) json_decref(pJSON_SourcesArray);
    if (pJSON_Source) json_decref(pJSON_Source);

    return cc;
}

/**
 * Frees the account settings structure, along with its contents.
 */
void ABC_AccountSettingsFree(tABC_AccountSettings *pSettings)
{
    if (pSettings)
    {
        ABC_FREE_STR(pSettings->szFirstName);
        ABC_FREE_STR(pSettings->szLastName);
        ABC_FREE_STR(pSettings->szFullName);
        ABC_FREE_STR(pSettings->szNickname);
        ABC_FREE_STR(pSettings->szLanguage);
        ABC_FREE_STR(pSettings->szPIN);
        if (pSettings->exchangeRateSources.aSources)
        {
            for (unsigned i = 0; i < pSettings->exchangeRateSources.numSources; i++)
            {
                ABC_FREE_STR(pSettings->exchangeRateSources.aSources[i]->szSource);
                ABC_CLEAR_FREE(pSettings->exchangeRateSources.aSources[i], sizeof(tABC_ExchangeRateSource));
            }
            ABC_CLEAR_FREE(pSettings->exchangeRateSources.aSources, sizeof(tABC_ExchangeRateSource *) * pSettings->exchangeRateSources.numSources);
        }

        ABC_CLEAR_FREE(pSettings, sizeof(tABC_AccountSettings));
    }
}

} // namespace abcd
