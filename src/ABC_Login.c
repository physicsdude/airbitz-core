/**
 * @file
 * AirBitz Account functions.
 *
 * This file contains all of the functions associated with account creation,
 * viewing and modification.
 *
 * @author Adam Harris
 * @version 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <jansson.h>
#include "ABC_Login.h"
#include "ABC_Util.h"
#include "ABC_FileIO.h"
#include "ABC_Crypto.h"
#include "ABC_URL.h"
#include "ABC_Debug.h"
#include "ABC_ServerDefs.h"
#include "ABC_Sync.h"
#include "ABC_Wallet.h"
#include "ABC_General.h"
#include "ABC_Mutex.h"

#define SYNC_SERVER "http://192.237.168.82/repos/"

#define ACCOUNT_MK_LENGTH 32

#define ACCOUNT_MAX                             1024  // maximum number of accounts
#define ACCOUNT_DIR                             "Accounts"
#define ACCOUNT_SYNC_DIR                        "sync"
#define ACCOUNT_FOLDER_PREFIX                   "Account_"
#define ACCOUNT_NAME_FILENAME                   "User_Name.json"
#define ACCOUNT_EREPO_FILENAME                  "ERepoAcctKey.json"
#define ACCOUNT_CARE_PACKAGE_FILENAME           "Care_Package.json"
#define ACCOUNT_LOGIN_PACKAGE_FILENAME          "Login_Package.json"
#define ACCOUNT_WALLETS_FILENAME                "Wallets.json"
#define ACCOUNT_CATEGORIES_FILENAME             "Categories.json"
#define ACCOUNT_ELP2_FILENAME                   "ELP2.json"
#define ACCOUNT_ELRA2_FILENAME                  "ELRA2.json"
#define ACCOUNT_SETTINGS_FILENAME               "Settings.json"

#define JSON_ACCT_USERNAME_FIELD                "userName"
#define JSON_ACCT_PIN_FIELD                     "PIN"
#define JSON_ACCT_REPO_FIELD                    "RepoAcctKey"
#define JSON_ACCT_WALLETS_FIELD                 "wallets"
#define JSON_ACCT_CATEGORIES_FIELD              "categories"
#define JSON_ACCT_ERQ_FIELD                     "ERQ"
#define JSON_ACCT_SNRP_FIELD_PREFIX             "SNRP"
#define JSON_ACCT_QUESTIONS_FIELD               "questions"
#define JSON_ACCT_MK_FIELD                      "MK"
#define JSON_ACCT_SYNCKEY_FIELD                 "SyncKey"

#define JSON_ACCT_FIRST_NAME_FIELD              "firstName"
#define JSON_ACCT_LAST_NAME_FIELD               "lastName"
#define JSON_ACCT_NICKNAME_FIELD                "nickname"
#define JSON_ACCT_NAME_ON_PAYMENTS_FIELD        "nameOnPayments"
#define JSON_ACCT_MINUTES_AUTO_LOGOUT_FIELD     "minutesAutoLogout"
#define JSON_ACCT_LANGUAGE_FIELD                "language"
#define JSON_ACCT_NUM_CURRENCY_FIELD            "numCurrency"
#define JSON_ACCT_EX_RATE_SOURCES_FIELD         "exchangeRateSources"
#define JSON_ACCT_EX_RATE_SOURCE_FIELD          "exchangeRateSource"
#define JSON_ACCT_BITCOIN_DENOMINATION_FIELD    "bitcoinDenomination"
#define JSON_ACCT_LABEL_FIELD                   "label"
#define JSON_ACCT_LABEL_TYPE                    "labeltype"
#define JSON_ACCT_SATOSHI_FIELD                 "satoshi"
#define JSON_ACCT_ADVANCED_FEATURES_FIELD       "advancedFeatures"
#define JSON_ACCT_CARE_PACKAGE                  "care_package"
#define JSON_ACCT_EREPO_ACCOUNT_FIELD           "erepo_account_key"

// holds keys for a given account
typedef struct sAccountKeys
{
    int             accountNum; // this is the number in the account directory - Account_x
    char            *szUserName;
    char            *szPassword;
    char            *szPIN;
    char            *szRepoAcctKey;
    tABC_CryptoSNRP *pSNRP1;
    tABC_CryptoSNRP *pSNRP2;
    tABC_CryptoSNRP *pSNRP3;
    tABC_CryptoSNRP *pSNRP4;
    tABC_U08Buf     MK;
    tABC_U08Buf     L;
    tABC_U08Buf     L1;
    tABC_U08Buf     P;
    tABC_U08Buf     P1;
    tABC_U08Buf     LRA;
    tABC_U08Buf     LRA1;
    tABC_U08Buf     L2;
    tABC_U08Buf     RQ;
    tABC_U08Buf     LP;
    tABC_U08Buf     LP2;
    tABC_U08Buf     LRA2;
} tAccountKeys;

// When a recovers password from on a new device, care package is stored either
// rather than creating an account directory
static char *gCarePackageCache = NULL;

// this holds all the of the currently cached account keys
static unsigned int gAccountKeysCacheCount = 0;
static tAccountKeys **gaAccountKeysCacheArray = NULL;

static tABC_CC ABC_LoginFetch(tABC_LoginRequestInfo *pInfo, tABC_Error *pError);
static tABC_CC ABC_LoginFetchInitCarePackage(const char *szUserName, tABC_U08Buf L1, const char *szCarePackage, char **szAccountDir, tABC_Error *pError);
static tABC_CC ABC_LoginFetchRepoKey(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, const char *szAccountDir, tABC_Error *pError);
static tABC_CC ABC_LoginRepoSetup(const char *szUserName, const char *szAccountDir, tABC_Error *pError);
static tABC_CC ABC_LoginFetchRecoveryQuestions(const char *szUserName, char **szRecoveryQuestions, tABC_Error *pError);
static tABC_CC ABC_LoginServerCreate(tABC_U08Buf L1, tABC_U08Buf P1, const char *szCarePackage_JSON, const char *szLoginPackage_JSON, char *szRepoAcctKey, tABC_Error *pError);
static tABC_CC ABC_LoginServerChangePassword(tABC_U08Buf L1, tABC_U08Buf oldP1, tABC_U08Buf LRA1, tABC_U08Buf newP1, tABC_Error *pError);
static tABC_CC ABC_LoginServerUploadCarePackage(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, const char *szAccountDir, tABC_Error *pError);
static tABC_CC ABC_LoginServerSetRecovery(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, const char *szCarePackage, const char *szLoginPackage, tABC_Error *pError);
static tABC_CC ABC_LoginCreateCarePackageJSONString(const json_t *pJSON_ERQ, const json_t *pJSON_SNRP2, const json_t *pJSON_SNRP3, const json_t *pJSON_SNRP4, char **pszJSON, tABC_Error *pError);
static tABC_CC ABC_LoginCreateLoginPackageJSONString(const json_t *pJSON_MK, const json_t *pJSON_RepoAcctKey, char **pszJSON, tABC_Error *pError);
static tABC_CC ABC_LoginGetCarePackageObjects(int AccountNum, const char *szCarePackage, json_t **ppJSON_ERQ, json_t **ppJSON_SNRP2, json_t **ppJSON_SNRP3, json_t **ppJSON_SNRP4, tABC_Error *pError);
static tABC_CC ABC_LoginGetLoginPackageObjects(int AccountNum, const char *szLoginPackage, json_t **ppJSON_EMK, json_t **ppJSON_ESyncKey, tABC_Error *pError);
static tABC_CC ABC_LoginCreateSync(const char *szAccountsRootDir, bool bIncludeDefs, tABC_Error *pError);
static tABC_CC ABC_LoginNextAccountNum(int *pAccountNum, tABC_Error *pError);
static tABC_CC ABC_LoginCreateRootDir(tABC_Error *pError);
static tABC_CC ABC_LoginCopyRootDirName(char *szRootDir, tABC_Error *pError);
static tABC_CC ABC_LoginCopyAccountDirName(char *szAccountDir, int AccountNum, tABC_Error *pError);
static tABC_CC ABC_LoginCreateListJSON(const char *szName, const char *szItems, char **pszJSON,  tABC_Error *pError);
static tABC_CC ABC_LoginNumForUser(const char *szUserName, int *pAccountNum, tABC_Error *pError);
static tABC_CC ABC_LoginUserForNum(unsigned int AccountNum, char **pszUserName, tABC_Error *pError);
static tABC_CC ABC_LoginCacheKeys(const char *szUserName, const char *szPassword, tAccountKeys **ppKeys, tABC_Error *pError);
static void    ABC_LoginFreeAccountKeys(tAccountKeys *pAccountKeys);
static tABC_CC ABC_LoginAddToKeyCache(tAccountKeys *pAccountKeys, tABC_Error *pError);
static tABC_CC ABC_LoginKeyFromCacheByName(const char *szUserName, tAccountKeys **ppAccountKeys, tABC_Error *pError);
static tABC_CC ABC_LoginSaveCategories(const char *szUserName, char **aszCategories, unsigned int Count, tABC_Error *pError);
static tABC_CC ABC_LoginServerGetCarePackage(tABC_U08Buf L1, char **szResponse, tABC_Error *pError);
static tABC_CC ABC_LoginServerGetRepoAcctKey(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, char **szERepoAcctKey, tABC_Error *pError);
static tABC_CC ABC_LoginServerGetString(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, char *szURL, char *szField, char **szResponse, tABC_Error *pError);
static tABC_CC ABC_LoginGetSettingsFilename(const char *szUserName, char **pszFilename, tABC_Error *pError);
static tABC_CC ABC_LoginCreateDefaultSettings(tABC_LoginSettings **ppSettings, tABC_Error *pError);
static tABC_CC ABC_LoginLoadSettingsEnc(const char *szUserName, tABC_U08Buf Key, tABC_LoginSettings **ppSettings, tABC_Error *pError);
static tABC_CC ABC_LoginSaveSettingsEnc(const char *szUserName, tABC_U08Buf Key, tABC_LoginSettings *pSettings, tABC_Error *pError);
static tABC_CC ABC_LoginMutexLock(tABC_Error *pError);
static tABC_CC ABC_LoginMutexUnlock(tABC_Error *pError);

/**
 * Allocates and fills in an account request structure with the info given.
 *
 * @param ppAccountRequestInfo      Pointer to store allocated request info
 * @param requestType               Type of request this is being used for
 * @param szUserName                UserName for the account
 * @param szPassword                Password for the account (can be NULL for some requests)
 * @param szRecoveryQuestions       Recovery questions seperated by newlines (can be NULL for some requests)
 * @param szRecoveryAnswers         Recovery answers seperated by newlines (can be NULL for some requests)
 * @param szPIN                     PIN number for the account (can be NULL for some requests)
 * @param szNewPassword             New password for the account (for change password requests)
 * @param fRequestCallback          The function that will be called when the account create process has finished.
 * @param pData                     Pointer to data to be returned back in callback
 * @param pError                    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginRequestInfoAlloc(tABC_LoginRequestInfo **ppAccountRequestInfo,
                                    tABC_RequestType requestType,
                                    const char *szUserName,
                                    const char *szPassword,
                                    const char *szRecoveryQuestions,
                                    const char *szRecoveryAnswers,
                                    const char *szPIN,
                                    const char *szNewPassword,
                                    tABC_Request_Callback fRequestCallback,
                                    void *pData,
                                    tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_NULL(ppAccountRequestInfo);
    ABC_CHECK_NULL(szUserName);

    tABC_LoginRequestInfo *pAccountRequestInfo = NULL;
    ABC_ALLOC(pAccountRequestInfo, sizeof(tABC_LoginRequestInfo));

    pAccountRequestInfo->requestType = requestType;

    ABC_STRDUP(pAccountRequestInfo->szUserName, szUserName);

    if (NULL != szPassword)
    {
        ABC_STRDUP(pAccountRequestInfo->szPassword, szPassword);
    }

    if (NULL != szRecoveryQuestions)
    {
        ABC_STRDUP(pAccountRequestInfo->szRecoveryQuestions, szRecoveryQuestions);
    }

    if (NULL != szRecoveryAnswers)
    {
        ABC_STRDUP(pAccountRequestInfo->szRecoveryAnswers, szRecoveryAnswers);
    }

    if (NULL != szPIN)
    {
        ABC_STRDUP(pAccountRequestInfo->szPIN, szPIN);
    }

    if (NULL != szNewPassword)
    {
        ABC_STRDUP(pAccountRequestInfo->szNewPassword, szNewPassword);
    }

    pAccountRequestInfo->pData = pData;

    pAccountRequestInfo->fRequestCallback = fRequestCallback;

    *ppAccountRequestInfo = pAccountRequestInfo;

exit:

    return cc;
}

/**
 * Frees the account creation info structure
 */
void ABC_LoginRequestInfoFree(tABC_LoginRequestInfo *pAccountRequestInfo)
{
    if (pAccountRequestInfo)
    {
        ABC_FREE_STR(pAccountRequestInfo->szUserName);

        ABC_FREE_STR(pAccountRequestInfo->szPassword);

        ABC_FREE_STR(pAccountRequestInfo->szRecoveryQuestions);

        ABC_FREE_STR(pAccountRequestInfo->szRecoveryAnswers);

        ABC_FREE_STR(pAccountRequestInfo->szPIN);

        ABC_FREE_STR(pAccountRequestInfo->szNewPassword);

        ABC_CLEAR_FREE(pAccountRequestInfo, sizeof(tABC_LoginRequestInfo));
    }
}

/**
 * Performs the request specified. Assumes it is running in a thread.
 *
 * The function assumes it is in it's own thread (i.e., thread safe)
 * The callback will be called when it has finished.
 * The caller needs to handle potentially being in a seperate thread
 *
 * @param pData Structure holding all the data needed to create an account (should be a tABC_LoginCreateInfo)
 */
void *ABC_LoginRequestThreaded(void *pData)
{
    tABC_LoginRequestInfo *pInfo = (tABC_LoginRequestInfo *)pData;
    if (pInfo)
    {
        tABC_RequestResults results;

        results.requestType = pInfo->requestType;

        results.bSuccess = false;

        tABC_CC CC = ABC_CC_Error;

        // perform the appropriate request
        if (ABC_RequestType_CreateAccount == pInfo->requestType)
        {
            // create the account
            CC = ABC_LoginCreate(pInfo, &(results.errorInfo));
        }
        else if (ABC_RequestType_AccountSignIn == pInfo->requestType)
        {
            // sign-in
            CC = ABC_LoginSignIn(pInfo, &(results.errorInfo));
        }
        else if (ABC_RequestType_SetAccountRecoveryQuestions == pInfo->requestType)
        {
            // set the recovery information
            CC = ABC_LoginSetRecovery(pInfo, &(results.errorInfo));
        }
        else if (ABC_RequestType_ChangePassword == pInfo->requestType)
        {
            // change the password
            CC = ABC_LoginChangePassword(pInfo, &(results.errorInfo));
        }


        results.errorInfo.code = CC;

        // we are done so load up the info and ship it back to the caller via the callback
        results.pData = pInfo->pData;
        results.bSuccess = (CC == ABC_CC_Ok ? true : false);
        pInfo->fRequestCallback(&results);

        // it is our responsibility to free the info struct
        ABC_LoginRequestInfoFree(pInfo);
    }

    return NULL;
}

/**
 * Checks if the username and password are valid.
 *
 * If the login info is valid, the keys for this account
 * are also cached.
 * If the creditials are not valid, an error will be returned
 *
 * @param szUserName UserName for validation
 * @param szPassword Password for validation
 */
tABC_CC ABC_LoginCheckCredentials(const char *szUserName,
                                    const char *szPassword,
                                    tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szPassword);

    // check that this is a valid user
    ABC_CHECK_RET(ABC_LoginCheckValidUser(szUserName, pError));

    // cache up the keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, NULL, pError));

exit:

    return cc;
}

/**
 * Checks if the cached username and password are valid by decrypting a file with them.
 *
 * A remote device can change the password. This function will verify that the
 * password that is cached is still valid.
 *
 * @param szUserName UserName for validation
 * @param szPassword Password for validation
 */
tABC_CC ABC_LoginTestCredentials(const char *szUserName,
                                 const char *szPassword,
                                 tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tAccountKeys *pKeys = NULL;
    json_t *pJSON_EMK   = NULL;
    tABC_U08Buf MK      = ABC_BUF_NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szPassword);

    // check that this is a valid user
    ABC_CHECK_RET(ABC_LoginCheckValidUser(szUserName, pError));

    // cache up the keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));

    // Fetch MK from disk
    ABC_CHECK_RET(
        ABC_LoginGetLoginPackageObjects(
            pKeys->accountNum, NULL, &pJSON_EMK, NULL, pError));

    // Try to decrypt MK
    ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_EMK, pKeys->LP2, &MK, pError));
exit:
    if (pJSON_EMK) json_decref(pJSON_EMK);
    ABC_BUF_FREE(MK);

    return cc;
}


/**
 * Checks if the username is valid.
 *
 * If the username is not valid, an error will be returned
 *
 * @param szUserName UserName for validation
 */
tABC_CC ABC_LoginCheckValidUser(const char *szUserName,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_NULL(szUserName);

    int AccountNum = 0;

    // check locally for the account
    ABC_CHECK_RET(ABC_LoginNumForUser(szUserName, &AccountNum, pError));
    if (AccountNum < 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
    }

exit:

    return cc;
}

/**
 * Signs into an account
 * This cache's the keys for an account
 */
tABC_CC ABC_LoginSignIn(tABC_LoginRequestInfo *pInfo,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);
    int dataDirty;

    ABC_CHECK_NULL(pInfo);

    // Clear out any old data
    ABC_LoginClearKeyCache(NULL);

    // check that this is a valid user, ignore Error
    if (ABC_LoginCheckValidUser(pInfo->szUserName, NULL) != ABC_CC_Ok)
    {
        // Try the server
        ABC_CHECK_RET(ABC_LoginFetch(pInfo, pError));
        ABC_CHECK_RET(ABC_LoginCheckValidUser(pInfo->szUserName, pError));
    }
    else
    {
        // TODO: What happens on a network failure?
        // Note: No password so don't try to login, just update account repo
        ABC_LoginSyncData(pInfo->szUserName, NULL, &dataDirty, pError);
    }

    // check the credentials
    ABC_CHECK_RET(ABC_LoginCheckCredentials(pInfo->szUserName, pInfo->szPassword, pError));

    // take this non-blocking opportunity to update the info from the server if needed
    ABC_CHECK_RET(ABC_GeneralUpdateInfo(pError));

    // And finally sync the wallets data, ignore failures
    ABC_WalletSyncAll(pInfo->szUserName, pInfo->szPassword, &dataDirty, pError);
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_LoginClearKeyCache(NULL);
    }

    return cc;
}

/**
 * Create and account
 */
tABC_CC ABC_LoginCreate(tABC_LoginRequestInfo *pInfo,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tABC_GeneralInfo        *pGeneralInfo        = NULL;
    tABC_LoginSettings      *pSettings           = NULL;
    tAccountKeys            *pKeys               = NULL;
    json_t                  *pJSON_SNRP2         = NULL;
    json_t                  *pJSON_SNRP3         = NULL;
    json_t                  *pJSON_SNRP4         = NULL;
    json_t                  *pJSON_EMK           = NULL;
    json_t                  *pJSON_ESyncKey       = NULL;
    char                    *szCarePackage_JSON  = NULL;
    char                    *szLoginPackage_JSON = NULL;
    char                    *szJSON              = NULL;
    char                    *szERepoAcctKey     = NULL;
    char                    *szAccountDir        = NULL;
    char                    *szFilename          = NULL;
    char                    *szRepoURL           = NULL;

    int AccountNum = 0;
    tABC_U08Buf MK          = ABC_BUF_NULL;
    tABC_U08Buf RepoAcctKey = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(pInfo);


    // check locally that the account name is available
    ABC_CHECK_RET(ABC_LoginNumForUser(pInfo->szUserName, &AccountNum, pError));
    if (AccountNum >= 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountAlreadyExists, "Account already exists");
    }

    // create an account keys struct
    ABC_ALLOC(pKeys, sizeof(tAccountKeys));
    ABC_STRDUP(pKeys->szUserName, pInfo->szUserName);
    ABC_STRDUP(pKeys->szPassword, pInfo->szPassword);
    ABC_STRDUP(pKeys->szPIN, pInfo->szPIN);

    // generate the SNRP's
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP2), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP3), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP4), pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP2, &pJSON_SNRP2, pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP3, &pJSON_SNRP3, pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP4, &pJSON_SNRP4, pError));

    // L = username
    ABC_BUF_DUP_PTR(pKeys->L, pKeys->szUserName, strlen(pKeys->szUserName));
    //ABC_UtilHexDumpBuf("L", pKeys->L);

    // L1 = Scrypt(L, SNRP1)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    //ABC_UtilHexDumpBuf("L1", pKeys->L1);

    // P = password
    ABC_BUF_DUP_PTR(pKeys->P, pKeys->szPassword, strlen(pKeys->szPassword));
    //ABC_UtilHexDumpBuf("P", pKeys->P);

    // P1 = Scrypt(P, SNRP1)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->P, pKeys->pSNRP1, &(pKeys->P1), pError));
    //ABC_UtilHexDumpBuf("P1", pKeys->P1);

    // CarePackage = ERQ, SNRP2, SNRP3, SNRP4
    ABC_CHECK_RET(ABC_LoginCreateCarePackageJSONString(NULL, pJSON_SNRP2, pJSON_SNRP3, pJSON_SNRP4, &szCarePackage_JSON, pError));

    // LP = L + P
    ABC_BUF_DUP(pKeys->LP, pKeys->L);
    ABC_BUF_APPEND(pKeys->LP, pKeys->P);
    //ABC_UtilHexDumpBuf("LP", pKeys->LP);

    // L2 = Scrypt(L + P, SNRP4)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L2), pError));

    // LP2 = Scrypt(L + P, SNRP2)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP2, &(pKeys->LP2), pError));

    // find the next available account number on this device
    ABC_CHECK_RET(ABC_LoginNextAccountNum(&(pKeys->accountNum), pError));

    // create the main account directory
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));
    ABC_CHECK_RET(ABC_FileIOCreateDir(szAccountDir, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the name file data and write the file
    ABC_CHECK_RET(ABC_UtilCreateValueJSONString(pKeys->szUserName, JSON_ACCT_USERNAME_FIELD, &szJSON, pError));
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_NAME_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szJSON, pError));
    ABC_FREE_STR(szJSON);
    szJSON = NULL;

    // Create MK
    ABC_CHECK_RET(ABC_CryptoCreateRandomData(ACCOUNT_MK_LENGTH, &MK, pError));
    ABC_BUF_DUP(pKeys->MK, pKeys->MK);

    // Create Repo key
    ABC_CHECK_RET(ABC_CryptoCreateRandomData(SYNC_REPO_KEY_LENGTH, &RepoAcctKey, pError));
    ABC_CHECK_RET(ABC_CryptoHexEncode(RepoAcctKey, &(pKeys->szRepoAcctKey), pError));

    tABC_U08Buf RepoBuf = ABC_BUF_NULL;
    ABC_BUF_SET_PTR(RepoBuf, (unsigned char *)pKeys->szRepoAcctKey, strlen(pKeys->szRepoAcctKey) + 1);

    // create the EMK JSON, ERepoAcctKey, and LoginPackage_JSON
    ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(MK, pKeys->LP2, ABC_CryptoType_AES256, &pJSON_EMK, pError));
    ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(RepoBuf, pKeys->L2, ABC_CryptoType_AES256, &pJSON_ESyncKey, pError));
    ABC_CHECK_RET(ABC_LoginCreateLoginPackageJSONString(pJSON_EMK, pJSON_ESyncKey, &szLoginPackage_JSON, pError));

    // Create the repo and account on server
    ABC_CHECK_RET(ABC_LoginServerCreate(pKeys->L1, pKeys->P1,
                    szCarePackage_JSON, szLoginPackage_JSON,
                    pKeys->szRepoAcctKey, pError));

    // write the file care package to a file
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage_JSON, pError));

    // write the file login package to a file
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage_JSON, pError));

    ABC_CHECK_RET(ABC_LoginCreateSync(szAccountDir, true, pError));

    // we now have a new account so go ahead and cache it's keys
    ABC_CHECK_RET(ABC_LoginAddToKeyCache(pKeys, pError));

    // Saving PIN in settings
    ABC_CHECK_RET(ABC_LoadAccountSettings(pInfo->szUserName, pInfo->szPassword, &pSettings, pError));
    ABC_STRDUP(pSettings->szPIN, pInfo->szPIN);
    ABC_CHECK_RET(ABC_UpdateAccountSettings(pInfo->szUserName, pInfo->szPassword, pSettings, pError));

    // take this opportunity to download the questions they can choose from for recovery
    ABC_CHECK_RET(ABC_GeneralUpdateQuestionChoices(pError));

    // also take this non-blocking opportunity to update the info from the server if needed
    ABC_CHECK_RET(ABC_GeneralUpdateInfo(pError));

    // Load the general info
    ABC_CHECK_RET(ABC_GeneralGetInfo(&pGeneralInfo, pError));

    // Create
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Create Repo Path
    ABC_CHECK_RET(ABC_LoginPickRepo(pKeys->szRepoAcctKey, &szRepoURL, pError));
    ABC_DebugLog("Pushing to: %s\n", szRepoURL);

    // Init the git repo and sync it
    int dirty;
    ABC_CHECK_RET(ABC_SyncMakeRepo(szFilename, pError));
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, szRepoURL, &dirty, pError));

    pKeys = NULL; // so we don't free what we just added to the cache
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_FileIODeleteRecursive(szAccountDir, NULL);
    }
    if (pKeys)
    {
        ABC_LoginFreeAccountKeys(pKeys);
        ABC_CLEAR_FREE(pKeys, sizeof(tAccountKeys));
    }
    if (pJSON_SNRP2)        json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)        json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)        json_decref(pJSON_SNRP4);
    if (pJSON_EMK)          json_decref(pJSON_EMK);
    if (pJSON_ESyncKey)     json_decref(pJSON_ESyncKey);
    ABC_FREE_STR(szRepoURL);
    ABC_FREE_STR(szCarePackage_JSON);
    ABC_FREE_STR(szJSON);
    ABC_FREE_STR(szERepoAcctKey);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_GeneralFreeInfo(pGeneralInfo);
    ABC_LoginFreeSettings(pSettings);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Fetchs account from server
 *
 * @param szUserName   Login
 * @param szPassword   Password
 */
static
tABC_CC ABC_LoginFetch(tABC_LoginRequestInfo *pInfo, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szAccountDir      = NULL;
    char *szCarePackage     = NULL;
    tAccountKeys *pKeys     = NULL;
    tABC_U08Buf L           = ABC_BUF_NULL;
    tABC_U08Buf L1          = ABC_BUF_NULL;
    tABC_U08Buf L2          = ABC_BUF_NULL;
    tABC_U08Buf P           = ABC_BUF_NULL;
    tABC_U08Buf P1          = ABC_BUF_NULL;
    tABC_U08Buf NULL_LRA1   = ABC_BUF_NULL;
    tABC_CryptoSNRP *pSNRP1 = NULL;

    // Create L, P, SNRP1, L1, P1
    ABC_BUF_DUP_PTR(L, pInfo->szUserName, strlen(pInfo->szUserName));
    ABC_BUF_DUP_PTR(P, pInfo->szPassword, strlen(pInfo->szPassword));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(P, pSNRP1, &P1, pError));

    //  Download CarePackage.json and ERepoAcctKey.json
    ABC_CHECK_RET(ABC_LoginServerGetCarePackage(L1, &szCarePackage, pError));

    // Setup initial account and fetch care package
    ABC_CHECK_RET(ABC_LoginFetchInitCarePackage(pInfo->szUserName, L1, szCarePackage, &szAccountDir, pError));

    // We have the care package so fetch keys without password
    ABC_CHECK_RET(ABC_LoginCacheKeys(pInfo->szUserName, NULL, &pKeys, pError));

    // L2 = Scrypt(L, SNRP4)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pKeys->pSNRP4, &L2, pError));

    // Fetch the ERepoAcctKey
    ABC_CHECK_RET(ABC_LoginFetchRepoKey(L1, P1, NULL_LRA1, szAccountDir, pError));

    // Setup the account repo and sync
    ABC_CHECK_RET(ABC_LoginRepoSetup(pInfo->szUserName, szAccountDir, pError));

    // Fetch and Sync Wallets
    int dirty;
    ABC_CHECK_RET(ABC_WalletSyncAll(pInfo->szUserName, pInfo->szPassword, &dirty, pError));

    // Clear out cache so its reloaded with the password
    ABC_CHECK_RET(ABC_LoginClearKeyCache(pError));
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_FileIODeleteRecursive(szAccountDir, NULL);
        ABC_LoginClearKeyCache(NULL);
    }
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szCarePackage);
    ABC_BUF_FREE(L);
    ABC_BUF_FREE(L1);
    ABC_BUF_FREE(P);
    ABC_BUF_FREE(P1);
    ABC_BUF_FREE(L2);
    ABC_CryptoFreeSNRP(&pSNRP1);

    return cc;
}

static
tABC_CC ABC_LoginFetchInitCarePackage(const char *szUserName, tABC_U08Buf L1, const char *szCarePackage, char **szAccountDir, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szFilename    = NULL;
    char *szJSON        = NULL;
    int AccountNum      = 0;

    // find the next available account number on this device
    ABC_CHECK_RET(ABC_LoginNextAccountNum(&AccountNum, pError));

    // create the main account directory
    ABC_ALLOC(*szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(*szAccountDir, AccountNum, pError));
    ABC_CHECK_RET(ABC_FileIOCreateDir(*szAccountDir, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the name file data and write the file
    ABC_CHECK_RET(ABC_UtilCreateValueJSONString(szUserName, JSON_ACCT_USERNAME_FIELD, &szJSON, pError));
    sprintf(szFilename, "%s/%s", *szAccountDir, ACCOUNT_NAME_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szJSON, pError));

    //  Save Care Package
    sprintf(szFilename, "%s/%s", *szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage, pError));
exit:
    ABC_FREE_STR(szJSON);
    return cc;
}

static
tABC_CC ABC_LoginFetchRepoKey(tABC_U08Buf L1, tABC_U08Buf P1,
                                tABC_U08Buf LRA1,
                                const char *szAccountDir,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szFilename        = NULL;
    char *szERepoAcctKey    = NULL;
    bool bExists            = false;

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    //  Do we already have an ERepoAcctKey?
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_EREPO_FILENAME);
    ABC_CHECK_RET(ABC_FileIOFileExists(szFilename, &bExists, pError));
    if (!bExists)
    {
        // Fetch ERepoAcctKey using P1 or LRA1
        ABC_CHECK_RET(ABC_LoginServerGetRepoAcctKey(L1, P1, LRA1, &szERepoAcctKey, pError));

        // Save ERepoAcctKey
        ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szERepoAcctKey, pError));
    }
exit:
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szERepoAcctKey);
    return cc;
}

static
tABC_CC ABC_LoginRepoSetup(const char *szUserName,
                             const char *szAccountDir,
                             tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    tABC_U08Buf L2          = ABC_BUF_NULL;
    char *szFilename        = NULL;
    char *szRepoAcctKey     = NULL;
    char *szREPO_JSON       = NULL;
    char *szRepoURL         = NULL;
    tABC_U08Buf REPO_JSON   = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, NULL, ABC_LoginKey_L2, &L2, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_EREPO_FILENAME);

    // Decrypt ERepoAcctKey
    tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONFile(szFilename, L2, &REPO_JSON, pError);
    // check the results
    if (ABC_CC_DecryptFailure == CC_Decrypt)
    {
        ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt RepoAcctKey - bad password");
    }
    else if (ABC_CC_Ok != CC_Decrypt)
    {
        cc = CC_Decrypt;
        goto exit;
    }

    char *szJSON_REPO = (char *) ABC_BUF_PTR(REPO_JSON);
    ABC_CHECK_RET(ABC_UtilGetStringValueFromJSONString(szJSON_REPO, JSON_ACCT_REPO_FIELD, &szRepoAcctKey, pError));

    //  Create sync directory and sync
    ABC_CHECK_RET(ABC_LoginCreateSync(szAccountDir, false, pError));
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Create repo URL
    ABC_CHECK_RET(ABC_LoginPickRepo(szRepoAcctKey, &szRepoURL, pError));

    ABC_DebugLog("Fetching from: %s\n", szRepoURL);

    // Init the git repo and sync it
    int dirty;
    ABC_CHECK_RET(ABC_SyncMakeRepo(szFilename, pError));
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, szRepoURL, &dirty, pError));
exit:
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szREPO_JSON);
    ABC_FREE_STR(szRepoAcctKey);
    ABC_FREE_STR(szRepoURL);
    ABC_BUF_FREE(REPO_JSON);
    return cc;
}

/**
 * Creates an account on the server.
 *
 * This function sends information to the server to create an account.
 * If the account was created, ABC_CC_Ok is returned.
 * If the account already exists, ABC_CC_AccountAlreadyExists is returned.
 *
 * @param L1   Login hash for the account
 * @param P1   Password hash for the account
 */
static
tABC_CC ABC_LoginServerCreate(tABC_U08Buf L1, tABC_U08Buf P1,
                              const char *szCarePackage_JSON,
                              const char *szLoginPackage_JSON,
                              char *szRepoAcctKey,
                              tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szURL     = NULL;
    char *szResults = NULL;
    char *szPost    = NULL;
    char *szL1_Base64 = NULL;
    char *szP1_Base64 = NULL;
    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL_BUF(L1);
    ABC_CHECK_NULL_BUF(P1);

    // create the URL
    ABC_ALLOC(szURL, ABC_URL_MAX_PATH_LENGTH);
    sprintf(szURL, "%s/%s", ABC_SERVER_ROOT, ABC_SERVER_ACCOUNT_CREATE_PATH);

    // create base64 versions of L1 and P1
    ABC_CHECK_RET(ABC_CryptoBase64Encode(L1, &szL1_Base64, pError));
    ABC_CHECK_RET(ABC_CryptoBase64Encode(P1, &szP1_Base64, pError));

    // create the post data
    pJSON_Root = json_pack("{ssssssssss}",
                        ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                        ABC_SERVER_JSON_P1_FIELD, szP1_Base64,
                        ABC_SERVER_JSON_CARE_PACKAGE_FIELD, szCarePackage_JSON,
                        ABC_SERVER_JSON_LOGIN_PACKAGE_FIELD, szLoginPackage_JSON,
                        ABC_SERVER_JSON_REPO_FIELD, szRepoAcctKey);
    szPost = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_COMPACT);
    json_decref(pJSON_Root);
    pJSON_Root = NULL;
    ABC_DebugLog("Server URL: %s, Data: %s", szURL, szPost);

    // send the command
    //ABC_CHECK_RET(ABC_URLPostString("http://httpbin.org/post", szPost, &szResults, pError));
    //ABC_DebugLog("Results: %s", szResults);
    ABC_CHECK_RET(ABC_URLPostString(szURL, szPost, &szResults, pError));
    ABC_DebugLog("Server results: %s", szResults);

    // decode the result
    json_t *pJSON_Value = NULL;
    json_error_t error;
    pJSON_Root = json_loads(szResults, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing server JSON");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON");

    // get the status code
    pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_STATUS_CODE_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_number(pJSON_Value)), ABC_CC_JSONError, "Error parsing server JSON status code");
    int statusCode = (int) json_integer_value(pJSON_Value);

    // if there was a failure
    if (ABC_Server_Code_Success != statusCode)
    {
        if (ABC_Server_Code_AccountExists == statusCode)
        {
            ABC_RET_ERROR(ABC_CC_AccountAlreadyExists, "Account already exists on server");
        }
        else
        {
            // get the message
            pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_MESSAGE_FIELD);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_DebugLog("Server message: %s", json_string_value(pJSON_Value));
            ABC_RET_ERROR(ABC_CC_ServerError, json_string_value(pJSON_Value));
        }
    }

exit:
    ABC_FREE_STR(szURL);
    ABC_FREE_STR(szResults);
    ABC_FREE_STR(szPost);
    ABC_FREE_STR(szL1_Base64);
    ABC_FREE_STR(szP1_Base64);
    if (pJSON_Root)     json_decref(pJSON_Root);

    return cc;
}

/**
 * Set the recovery questions for an account
 *
 * This function sets the password recovery informatin for the account.
 * This includes sending a new care package to the server
 *
 * @param pInfo     Pointer to recovery information data
 * @param pError    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginSetRecovery(tABC_LoginRequestInfo *pInfo,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys    *pKeys               = NULL;
    json_t          *pJSON_ERQ           = NULL;
    json_t          *pJSON_SNRP2         = NULL;
    json_t          *pJSON_SNRP3         = NULL;
    json_t          *pJSON_SNRP4         = NULL;
    char            *szCarePackage_JSON  = NULL;
    char            *szLoginPackage_JSON = NULL;
    char            *szELP2_JSON         = NULL;
    char            *szELRA2_JSON        = NULL;
    char            *szAccountDir        = NULL;
    char            *szFilename          = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(pInfo);

    int AccountNum = 0;

    // check locally for the account
    ABC_CHECK_RET(ABC_LoginNumForUser(pInfo->szUserName, &AccountNum, pError));
    if (AccountNum < 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
    }

    // cache up the keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(pInfo->szUserName, pInfo->szPassword, &pKeys, pError));

    // the following should all be available
    // szUserName, szPassword, szPIN, L, P, LP2, SNRP2, SNRP3, SNRP4
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->P), ABC_CC_Error, "Expected to find P in key cache");
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP2), ABC_CC_Error, "Expected to find LP2 in key cache");
    ABC_CHECK_ASSERT(NULL != pKeys->pSNRP3, ABC_CC_Error, "Expected to find SNRP3 in key cache");
    ABC_CHECK_ASSERT(NULL != pKeys->pSNRP4, ABC_CC_Error, "Expected to find SNRP4 in key cache");

    // Create the keys that we still need or that need to be updated

    // SNRP1
    if (NULL == pKeys->pSNRP1)
    {
        ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
    }

    // LRA = L + RA
    if (ABC_BUF_PTR(pKeys->LRA) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA);
    }
    ABC_BUF_DUP(pKeys->LRA, pKeys->L);
    ABC_BUF_APPEND_PTR(pKeys->LRA, pInfo->szRecoveryAnswers, strlen(pInfo->szRecoveryAnswers));
    //ABC_DEBUG(ABC_UtilHexDumpBuf("LRA", pKeys->LRA));

    // LRA1 = Scrypt(L + RA, SNRP1)
    if (ABC_BUF_PTR(pKeys->LRA1) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA1);
    }
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LRA, pKeys->pSNRP1, &(pKeys->LRA1), pError));


    // LRA2 = Scrypt(L + RA, SNRP3)
    if (ABC_BUF_PTR(pKeys->LRA2) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA2);
    }
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LRA, pKeys->pSNRP3, &(pKeys->LRA2), pError));

    // L2 = Scrypt(L, SNRP4)
    if (ABC_BUF_PTR(pKeys->L2) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L2), pError));
    }

    // RQ
    if (ABC_BUF_PTR(pKeys->RQ) != NULL)
    {
        ABC_BUF_FREE(pKeys->RQ);
    }
    ABC_BUF_DUP_PTR(pKeys->RQ, pInfo->szRecoveryQuestions, strlen(pInfo->szRecoveryQuestions));
    //ABC_UtilHexDumpBuf("RQ", pKeys->RQ);

    // L1 = Scrypt(L, SNRP1)
    if (ABC_BUF_PTR(pKeys->L1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    }

    // P1 = Scrypt(P, SNRP1)
    if (ABC_BUF_PTR(pKeys->P1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->P, pKeys->pSNRP1, &(pKeys->P1), pError));
    }

    // create the json objects and strings we need

    // ERQ = AES256(RQ, L2)
    ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(pKeys->RQ, pKeys->L2, ABC_CryptoType_AES256, &pJSON_ERQ, pError));

    // ELP2 = AES256(LP2, LRA2)
    ABC_CHECK_RET(ABC_CryptoEncryptJSONString(pKeys->LP2, pKeys->LRA2, ABC_CryptoType_AES256, &szELP2_JSON, pError));
    //ABC_UtilHexDumpBuf("LP2", pKeys->LP2);

    // ELRA2 = AES256(LRA2, LP2)
    ABC_CHECK_RET(ABC_CryptoEncryptJSONString(pKeys->LRA2, pKeys->LP2, ABC_CryptoType_AES256, &szELRA2_JSON, pError));


    // write out the files

    // create the main account directory
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // write ELP2.json <- LP2 (L+P,S2) encrypted with recovery key (LRA2)
    sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELP2_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szELP2_JSON, pError));

    // write ELRA2.json <- LRA2 encrypted with LP2 (L+P,S2)
    sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELRA2_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szELRA2_JSON, pError));


    // update the care package

    // get the current care package
    ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(AccountNum, NULL, NULL, &pJSON_SNRP2, &pJSON_SNRP3, &pJSON_SNRP4, pError));

    // Create an updated CarePackage = ERQ, SNRP2, SNRP3, SNRP4
    ABC_CHECK_RET(ABC_LoginCreateCarePackageJSONString(pJSON_ERQ, pJSON_SNRP2, pJSON_SNRP3, pJSON_SNRP4, &szCarePackage_JSON, pError));

    // write the file care package to a file
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage_JSON, pError));

    // read in the login package
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOReadFileStr(szFilename, &szLoginPackage_JSON, pError));

    // Client sends L1, P1, LRA1, CarePackage, to the server
    ABC_CHECK_RET(
        ABC_LoginServerSetRecovery(pKeys->L1, pKeys->P1, pKeys->LRA1,
                                   szCarePackage_JSON, szLoginPackage_JSON,
                                   pError));

    // Sync the data (ELP2 and ELRA2) with server
    int dirty;
    ABC_CHECK_RET(ABC_LoginSyncData(pKeys->szUserName, pKeys->szPassword, &dirty, pError));
    ABC_CHECK_RET(ABC_WalletSyncAll(pKeys->szUserName, pKeys->szPassword, &dirty, pError));
exit:
    if (pJSON_ERQ)          json_decref(pJSON_ERQ);
    if (pJSON_SNRP2)        json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)        json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)        json_decref(pJSON_SNRP4);
    ABC_FREE_STR(szCarePackage_JSON);
    ABC_FREE_STR(szLoginPackage_JSON);
    ABC_FREE_STR(szELP2_JSON);
    ABC_FREE_STR(szELRA2_JSON);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Change password for an account
 *
 * This function sets the password recovery informatin for the account.
 * This includes sending a new care package to the server
 *
 * @param pInfo     Pointer to password change information data
 * @param pError    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginChangePassword(tABC_LoginRequestInfo *pInfo,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys = NULL;
    tABC_U08Buf oldLP2  = ABC_BUF_NULL;
    tABC_U08Buf LRA2    = ABC_BUF_NULL;
    tABC_U08Buf LRA     = ABC_BUF_NULL;
    tABC_U08Buf LRA1    = ABC_BUF_NULL;
    tABC_U08Buf oldP1   = ABC_BUF_NULL;
    tABC_U08Buf SettingsData = ABC_BUF_NULL;
    char *szAccountDir = NULL;
    char *szFilename = NULL;
    char *szSettingsFilename = NULL;
    char *szJSON = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(pInfo);
    ABC_CHECK_NULL(pInfo->szUserName);
    ABC_CHECK_NULL(pInfo->szNewPassword);

    // get the account directory and set up for creating needed filenames
    ABC_CHECK_RET(ABC_LoginGetDirName(pInfo->szUserName, &szAccountDir, pError));
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // get the keys for this user (note: password can be NULL for this call)
    ABC_CHECK_RET(ABC_LoginCacheKeys(pInfo->szUserName, pInfo->szPassword, &pKeys, pError));

    // we need to obtain the original LP2 and LRA2
    if (pInfo->szPassword != NULL)
    {
        // we had the password so we should have the LP2 key
        ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP2), ABC_CC_Error, "Expected to find LP2 in key cache");
        ABC_BUF_DUP(oldLP2, pKeys->LP2);

        // if we don't yet have LRA2
        if (ABC_BUF_PTR(pKeys->LRA2) == NULL)
        {
            // get the LRA2 by decrypting ELRA2
            sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELRA2_FILENAME);
            bool bExists = false;
            ABC_CHECK_RET(ABC_FileIOFileExists(szFilename, &bExists, pError));
            if (true == bExists)
            {
                ABC_CHECK_RET(ABC_CryptoDecryptJSONFile(szFilename, pKeys->LP2, &(pKeys->LRA2), pError));
            }
        }
        ABC_BUF_DUP(LRA2, pKeys->LRA2);

        // create the old P1 for use in server auth -> P1 = Scrypt(P, SNRP1)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->P, pKeys->pSNRP1, &oldP1, pError));
    }
    else
    {
        // we have the recovery questions so we can make the LRA2

        // LRA = L + RA
        ABC_BUF_DUP(LRA, pKeys->L);
        ABC_BUF_APPEND_PTR(LRA, pInfo->szRecoveryAnswers, strlen(pInfo->szRecoveryAnswers));

        // LRA2 = Scrypt(LRA, SNRP3)
        ABC_CHECK_ASSERT(NULL != pKeys->pSNRP3, ABC_CC_Error, "Expected to find SNRP3 in key cache");
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP3, &LRA2, pError));

        // get the LP2 by decrypting ELP2 with LRA2
        sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELP2_FILENAME);
        ABC_CHECK_RET(ABC_CryptoDecryptJSONFile(szFilename, LRA2, &oldLP2, pError));

        // create LRA1 as it will be needed for server communication later
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP1, &LRA1, pError));
    }

    // we now have oldLP2 and oldLRA2

    // time to set the new data for this account

    // set new PIN
    ABC_FREE_STR(pKeys->szPIN);
    ABC_STRDUP(pKeys->szPIN, pInfo->szPIN);

    // set new password
    ABC_FREE_STR(pKeys->szPassword);
    ABC_STRDUP(pKeys->szPassword, pInfo->szNewPassword);

    // set new P
    ABC_BUF_FREE(pKeys->P);
    ABC_BUF_DUP_PTR(pKeys->P, pKeys->szPassword, strlen(pKeys->szPassword));

    // set new P1
    ABC_BUF_FREE(pKeys->P1);
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->P, pKeys->pSNRP1, &(pKeys->P1), pError));

    // set new LP = L + P
    ABC_BUF_FREE(pKeys->LP);
    ABC_BUF_DUP(pKeys->LP, pKeys->L);
    ABC_BUF_APPEND(pKeys->LP, pKeys->P);

    // set new LP2 = Scrypt(L + P, SNRP2)
    ABC_BUF_FREE(pKeys->LP2);
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP2, &(pKeys->LP2), pError));

    // we'll need L1 for server communication L1 = Scrypt(L, SNRP1)
    if (ABC_BUF_PTR(pKeys->L1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    }

    // server change password - Server will need L1, (P1 or LRA1) and new_P1
    ABC_CHECK_RET(ABC_LoginServerChangePassword(pKeys->L1, oldP1, LRA1, pKeys->P1, pError));

    // change all the wallet keys - re-encrypted them with new LP2
    ABC_CHECK_RET(ABC_WalletChangeEMKsForAccount(pInfo->szUserName, oldLP2, pKeys->LP2, pError));

    if (ABC_BUF_PTR(LRA2) != NULL)
    {
        // write out new ELP2.json <- LP2 encrypted with recovery key (LRA2)
        sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELP2_FILENAME);
        ABC_CHECK_RET(ABC_CryptoEncryptJSONFile(pKeys->LP2, LRA2, ABC_CryptoType_AES256, szFilename, pError));

        // write out new ELRA2.json <- LRA2 encrypted with LP2 (L+P,S2)
        sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_ELRA2_FILENAME);
        ABC_CHECK_RET(ABC_CryptoEncryptJSONFile(LRA2, pKeys->LP2, ABC_CryptoType_AES256, szFilename, pError));
    }

    // re-encrypt the settings
    ABC_CHECK_RET(ABC_LoginGetSettingsFilename(pInfo->szUserName, &szSettingsFilename, pError));
    bool bExists = false;
    ABC_CHECK_RET(ABC_FileIOFileExists(szSettingsFilename, &bExists, pError));
    if (true == bExists)
    {
        // load them using the old key
        ABC_CHECK_RET(ABC_CryptoDecryptJSONFile(szSettingsFilename, oldLP2, &SettingsData, pError));

        // save them using the new key
        ABC_CHECK_RET(ABC_CryptoEncryptJSONFile(SettingsData, pKeys->LP2, ABC_CryptoType_AES256, szSettingsFilename, pError));
    }

    // the keys for the account have all been updated so other functions can now be called that use them

    // set the new PIN
    ABC_CHECK_RET(ABC_LoginSetPIN(pInfo->szUserName, pInfo->szNewPassword, pInfo->szPIN, pError));

    // Sync the data (ELP2 and ELRA2) with server
    int dirty;
    ABC_CHECK_RET(ABC_LoginSyncData(pInfo->szUserName, pInfo->szNewPassword, &dirty, pError));
    ABC_CHECK_RET(ABC_WalletSyncAll(pInfo->szUserName, pInfo->szNewPassword, &dirty, pError));
exit:
    ABC_BUF_FREE(oldLP2);
    ABC_BUF_FREE(LRA2);
    ABC_BUF_FREE(LRA);
    ABC_BUF_FREE(LRA1);
    ABC_BUF_FREE(oldP1);
    ABC_BUF_FREE(SettingsData);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szJSON);
    ABC_FREE_STR(szSettingsFilename);
    if (cc != ABC_CC_Ok) ABC_LoginClearKeyCache(NULL);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Changes the password for an account on the server.
 *
 * This function sends information to the server to change the password for an account.
 * Either the old P1 or LRA1 can be used for authentication.
 *
 * @param L1    Login hash for the account
 * @param oldP1 Old password hash for the account (if this is empty, LRA1 is used instead)
 * @param LRA1  LRA1 for the account (used if oldP1 is empty)
 */
static
tABC_CC ABC_LoginServerChangePassword(tABC_U08Buf L1, tABC_U08Buf oldP1, tABC_U08Buf LRA1, tABC_U08Buf newP1, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szURL     = NULL;
    char *szResults = NULL;
    char *szPost    = NULL;
    char *szL1_Base64 = NULL;
    char *szOldP1_Base64 = NULL;
    char *szNewP1_Base64 = NULL;
    char *szAuth_Base64 = NULL;
    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL_BUF(L1);
    ABC_CHECK_NULL_BUF(newP1);

    // create the URL
    ABC_ALLOC(szURL, ABC_URL_MAX_PATH_LENGTH);
    sprintf(szURL, "%s/%s", ABC_SERVER_ROOT, ABC_SERVER_CHANGE_PASSWORD_PATH);

    // create base64 versions of L1 and newP1
    ABC_CHECK_RET(ABC_CryptoBase64Encode(L1, &szL1_Base64, pError));
    ABC_CHECK_RET(ABC_CryptoBase64Encode(newP1, &szNewP1_Base64, pError));

    // create the post data
    if (ABC_BUF_PTR(oldP1) != NULL)
    {
        ABC_CHECK_RET(ABC_CryptoBase64Encode(oldP1, &szAuth_Base64, pError));
        pJSON_Root = json_pack("{ssssss}",
                               ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                               ABC_SERVER_JSON_P1_FIELD, szAuth_Base64,
                               ABC_SERVER_JSON_NEW_P1_FIELD, szNewP1_Base64);
    }
    else
    {
        ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(LRA1), ABC_CC_Error, "LRA1 missing for server password change auth");
        ABC_CHECK_RET(ABC_CryptoBase64Encode(LRA1, &szAuth_Base64, pError));
        pJSON_Root = json_pack("{ssssss}",
                               ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                               ABC_SERVER_JSON_LRA1_FIELD, szAuth_Base64,
                               ABC_SERVER_JSON_NEW_P1_FIELD, szNewP1_Base64);
    }
    szPost = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_COMPACT);
    json_decref(pJSON_Root);
    pJSON_Root = NULL;
    ABC_DebugLog("Server URL: %s, Data: %s", szURL, szPost);

    // send the command
    ABC_CHECK_RET(ABC_URLPostString(szURL, szPost, &szResults, pError));
    ABC_DebugLog("Server results: %s", szResults);

    // decode the result
    json_t *pJSON_Value = NULL;
    json_error_t error;
    pJSON_Root = json_loads(szResults, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing server JSON");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON");

    // get the status code
    pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_STATUS_CODE_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_number(pJSON_Value)), ABC_CC_JSONError, "Error parsing server JSON status code");
    int statusCode = (int) json_integer_value(pJSON_Value);

    // if there was a failure
    if (ABC_Server_Code_Success != statusCode)
    {
        // get the message
        pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_MESSAGE_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
        ABC_DebugLog("Server message: %s", json_string_value(pJSON_Value));
        ABC_RET_ERROR(ABC_CC_ServerError, json_string_value(pJSON_Value));
    }

exit:
    ABC_FREE_STR(szURL);
    ABC_FREE_STR(szResults);
    ABC_FREE_STR(szPost);
    ABC_FREE_STR(szL1_Base64);
    ABC_FREE_STR(szOldP1_Base64);
    ABC_FREE_STR(szNewP1_Base64);
    ABC_FREE_STR(szAuth_Base64);
    if (pJSON_Root)     json_decref(pJSON_Root);

    return cc;
}

/**
 * Set recovery questions and answers on the server.
 *
 * This function sends LRA1 and Care Package to the server as part
 * of setting up the recovery data for an account
 *
 * @param L1            Login hash for the account
 * @param P1            Password hash for the account
 * @param LRA1          Scrypt'ed login and recovery answers
 * @param szCarePackage Care Package for account
 */
static
tABC_CC ABC_LoginServerSetRecovery(tABC_U08Buf L1, tABC_U08Buf P1,
                                   tABC_U08Buf LRA1,
                                   const char *szCarePackage,
                                   const char *szLoginPackage,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szURL     = NULL;
    char *szResults = NULL;
    char *szPost    = NULL;
    char *szL1_Base64 = NULL;
    char *szP1_Base64 = NULL;
    char *szLRA1_Base64 = NULL;
    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL_BUF(L1);
    ABC_CHECK_NULL(szCarePackage);

    // create the URL
    ABC_ALLOC(szURL, ABC_URL_MAX_PATH_LENGTH);
    sprintf(szURL, "%s/%s", ABC_SERVER_ROOT, ABC_SERVER_UPDATE_CARE_PACKAGE_PATH);

    // create base64 versions of L1, P1 and LRA1
    ABC_CHECK_RET(ABC_CryptoBase64Encode(L1, &szL1_Base64, pError));
    ABC_CHECK_RET(ABC_CryptoBase64Encode(P1, &szP1_Base64, pError));
    if (ABC_BUF_PTR(LRA1) != NULL)
    {
        ABC_CHECK_RET(ABC_CryptoBase64Encode(LRA1, &szLRA1_Base64, pError));
        // create the post data
        pJSON_Root = json_pack("{ssssssssss}",
                            ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                            ABC_SERVER_JSON_P1_FIELD, szP1_Base64,
                            ABC_SERVER_JSON_LRA1_FIELD, szLRA1_Base64,
                            ABC_SERVER_JSON_CARE_PACKAGE_FIELD, szCarePackage,
                            ABC_SERVER_JSON_LOGIN_PACKAGE_FIELD, szLoginPackage);
    }
    else
    {
        // create the post data
        pJSON_Root = json_pack("{ssssssss}",
                            ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                            ABC_SERVER_JSON_P1_FIELD, szP1_Base64,
                            ABC_SERVER_JSON_CARE_PACKAGE_FIELD, szCarePackage,
                            ABC_SERVER_JSON_LOGIN_PACKAGE_FIELD, szLoginPackage);
    }
    szPost = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_COMPACT);
    json_decref(pJSON_Root);
    pJSON_Root = NULL;
    ABC_DebugLog("Server URL: %s, Data: %s", szURL, szPost);

    // send the command
    //ABC_CHECK_RET(ABC_URLPostString("http://httpbin.org/post", szPost, &szResults, pError));
    //ABC_DebugLog("Results: %s", szResults);
    ABC_CHECK_RET(ABC_URLPostString(szURL, szPost, &szResults, pError));
    ABC_DebugLog("Server results: %s", szResults);

    // decode the result
    json_t *pJSON_Value = NULL;
    json_error_t error;
    pJSON_Root = json_loads(szResults, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing server JSON");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON");

    // get the status code
    pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_STATUS_CODE_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_number(pJSON_Value)), ABC_CC_JSONError, "Error parsing server JSON status code");
    int statusCode = (int) json_integer_value(pJSON_Value);

    // if there was a failure
    if (ABC_Server_Code_Success != statusCode)
    {
        if (ABC_Server_Code_NoAccount == statusCode)
        {
            ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "Account does not exist on server");
        }
        else if (ABC_Server_Code_InvalidPassword == statusCode)
        {
            ABC_RET_ERROR(ABC_CC_BadPassword, "Invalid password on server");
        }
        else
        {
            // get the message
            pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_MESSAGE_FIELD);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_DebugLog("Server message: %s", json_string_value(pJSON_Value));
            ABC_RET_ERROR(ABC_CC_ServerError, json_string_value(pJSON_Value));
        }
    }

exit:
    ABC_FREE_STR(szURL);
    ABC_FREE_STR(szResults);
    ABC_FREE_STR(szPost);
    ABC_FREE_STR(szL1_Base64);
    ABC_FREE_STR(szP1_Base64);
    ABC_FREE_STR(szLRA1_Base64);
    if (pJSON_Root)     json_decref(pJSON_Root);

    return cc;
}

/**
 * Upload the care package
 *
 * This function sends LRA1 and Care Package to the server as part
 * of setting up the recovery data for an account
 *
 * @param L1            Login hash for the account
 * @param P1            Password hash for the account
 * @param LRA1          Scrypt'ed login and recovery answers
 * @param szCarePackage Care Package for account
 */
static
tABC_CC ABC_LoginServerUploadCarePackage(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1, const char *szAccountDir, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szCarePackage = NULL;
    char *szLoginPackage = NULL;
    char *szFilename = NULL;

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOReadFileStr(szFilename, &szCarePackage, pError));

    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOReadFileStr(szFilename, &szLoginPackage, pError));

    ABC_CHECK_RET(ABC_LoginServerSetRecovery(L1, P1, LRA1, szCarePackage, szLoginPackage, pError));
exit:
    ABC_FREE_STR(szCarePackage);
    ABC_FREE_STR(szFilename);
    return cc;
}


/**
 * Creates the JSON care package
 *
 * @param pJSON_ERQ    Pointer to ERQ JSON object
 *                     (if this is NULL, ERQ is not added to the care package)
 * @param pJSON_SNRP2  Pointer to SNRP2 JSON object
 * @param pJSON_SNRP3  Pointer to SNRP3 JSON object
 * @param pJSON_SNRP4  Pointer to SNRIP4 JSON object
 * @param pszJSON      Pointer to store allocated JSON for care package.
 *                     (the user is responsible for free'ing this pointer)
 */
static
tABC_CC ABC_LoginCreateCarePackageJSONString(const json_t *pJSON_ERQ,
                                               const json_t *pJSON_SNRP2,
                                               const json_t *pJSON_SNRP3,
                                               const json_t *pJSON_SNRP4,
                                               char         **pszJSON,
                                               tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *pJSON_Root = NULL;
    char *szField = NULL;

    ABC_CHECK_NULL(pJSON_SNRP2);
    ABC_CHECK_NULL(pJSON_SNRP3);
    ABC_CHECK_NULL(pJSON_SNRP4);
    ABC_CHECK_NULL(pszJSON);

    pJSON_Root = json_object();

    if (pJSON_ERQ != NULL)
    {
        json_object_set(pJSON_Root, JSON_ACCT_ERQ_FIELD, (json_t *) pJSON_ERQ);
    }

    ABC_ALLOC(szField, ABC_MAX_STRING_LENGTH);

    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 2);
    json_object_set(pJSON_Root, szField, (json_t *) pJSON_SNRP2);

    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 3);
    json_object_set(pJSON_Root, szField, (json_t *) pJSON_SNRP3);

    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 4);
    json_object_set(pJSON_Root, szField, (json_t *) pJSON_SNRP4);

    *pszJSON = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER);

exit:
    if (pJSON_Root) json_decref(pJSON_Root);
    ABC_FREE_STR(szField);

    return cc;
}

/**
 * Creates the JSON login package
 *
 * @param pJSON_MK           Pointer to MK JSON object
 * @param pJSON_SyncKey      Pointer to Repo Acct Key JSON object
 * @param pszJSON       Pointer to store allocated JSON for care package.
 *                     (the user is responsible for free'ing this pointer)
 */
static
tABC_CC ABC_LoginCreateLoginPackageJSONString(const json_t *pJSON_MK,
                                              const json_t *pJSON_SyncKey,
                                              char         **pszJSON,
                                              tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL(pJSON_MK);
    ABC_CHECK_NULL(pJSON_SyncKey);
    ABC_CHECK_NULL(pszJSON);

    pJSON_Root = json_object();

    json_object_set(pJSON_Root, JSON_ACCT_MK_FIELD, (json_t *) pJSON_MK);
    json_object_set(pJSON_Root, JSON_ACCT_SYNCKEY_FIELD, (json_t *) pJSON_SyncKey);

    *pszJSON = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER);
exit:
    if (pJSON_Root) json_decref(pJSON_Root);

    return cc;
}

// loads the json care package for a given account number
// if the ERQ doesn't exist, ppJSON_ERQ is set to NULL

/**
 * Loads the json care package for a given account number
 *
 * The JSON objects for each argument will be assigned.
 * The function assumes any number of the arguments may be NULL,
 * in which case, they are not set.
 * It is also possible that there is no recovery questions, in which case
 * the ERQ will be set to NULL.
 *
 * @param AccountNum   Account number of the account of interest
 * @param ppJSON_ERQ   Pointer store ERQ JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP2 Pointer store SNRP2 JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP3 Pointer store SNRP3 JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP4 Pointer store SNRP4 JSON object (can be NULL) - caller expected to decref
 * @param pError       A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginGetCarePackageObjects(int          AccountNum,
                                         const char   *szCarePackage,
                                         json_t       **ppJSON_ERQ,
                                         json_t       **ppJSON_SNRP2,
                                         json_t       **ppJSON_SNRP3,
                                         json_t       **ppJSON_SNRP4,
                                         tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountDir = NULL;
    char *szCarePackageFilename = NULL;
    char *szCarePackage_JSON = NULL;
    char *szField = NULL;
    json_t *pJSON_Root = NULL;
    json_t *pJSON_ERQ = NULL;
    json_t *pJSON_SNRP2 = NULL;
    json_t *pJSON_SNRP3 = NULL;
    json_t *pJSON_SNRP4 = NULL;

    // if we supply a care package, use it instead
    if (szCarePackage)
    {
        ABC_STRDUP(szCarePackage_JSON, szCarePackage);
    }
    else
    {
        ABC_CHECK_ASSERT(AccountNum >= 0, ABC_CC_AccountDoesNotExist, "Bad account number");

        // get the main account directory
        ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
        ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));

        // create the name of the care package file
        ABC_ALLOC(szCarePackageFilename, ABC_FILEIO_MAX_PATH_LENGTH);
        sprintf(szCarePackageFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);

        // load the care package
        ABC_CHECK_RET(ABC_FileIOReadFileStr(szCarePackageFilename, &szCarePackage_JSON, pError));
    }

    // decode the json
    json_error_t error;
    pJSON_Root = json_loads(szCarePackage_JSON, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing JSON care package");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON care package");

    // get the ERQ
    pJSON_ERQ = json_object_get(pJSON_Root, JSON_ACCT_ERQ_FIELD);
    //ABC_CHECK_ASSERT((pJSON_ERQ && json_is_object(pJSON_ERQ)), ABC_CC_JSONError, "Error parsing JSON care package - missing ERQ");

    ABC_ALLOC(szField, ABC_MAX_STRING_LENGTH);

    // get SNRP2
    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 2);
    pJSON_SNRP2 = json_object_get(pJSON_Root, szField);
    ABC_CHECK_ASSERT((pJSON_SNRP2 && json_is_object(pJSON_SNRP2)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP2");

    // get SNRP3
    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 3);
    pJSON_SNRP3 = json_object_get(pJSON_Root, szField);
    ABC_CHECK_ASSERT((pJSON_SNRP3 && json_is_object(pJSON_SNRP3)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP3");

    // get SNRP4
    sprintf(szField, "%s%d", JSON_ACCT_SNRP_FIELD_PREFIX, 4);
    pJSON_SNRP4 = json_object_get(pJSON_Root, szField);
    ABC_CHECK_ASSERT((pJSON_SNRP4 && json_is_object(pJSON_SNRP4)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP4");

    // assign what we found (we need to increment the refs because they were borrowed from root)
    if (ppJSON_ERQ)
    {
        if (pJSON_ERQ)
        {
            *ppJSON_ERQ = json_incref(pJSON_ERQ);
        }
        else
        {
            *ppJSON_ERQ = NULL;
        }
    }
    if (ppJSON_SNRP2)
    {
        *ppJSON_SNRP2 = json_incref(pJSON_SNRP2);
    }
    if (ppJSON_SNRP3)
    {
        *ppJSON_SNRP3 = json_incref(pJSON_SNRP3);
    }
    if (ppJSON_SNRP4)
    {
        *ppJSON_SNRP4 = json_incref(pJSON_SNRP4);
    }

exit:
    if (pJSON_Root)             json_decref(pJSON_Root);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szCarePackageFilename);
    ABC_FREE_STR(szCarePackage_JSON);
    ABC_FREE_STR(szField);

    return cc;
}

/**
 * Loads the json login package for a given account number
 *
 * The JSON objects for each argument will be assigned.
 * The function assumes any number of the arguments may be NULL,
 * in which case, they are not set.
 *
 * @param AccountNum      Account number of the account of interest
 * @param ppJSON_EMK      Pointer store EMK JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_ESyncKey Pointer store SNRP2 JSON object (can be NULL) - caller expected to decref
 * @param pError          A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginGetLoginPackageObjects(int          AccountNum,
                                        const char   *szLoginPackage,
                                        json_t       **ppJSON_EMK,
                                        json_t       **ppJSON_ESyncKey,
                                        tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountDir = NULL;
    char *szLoginPackageFilename = NULL;
    char *szLoginPackage_JSON = NULL;
    json_t *pJSON_Root = NULL;
    json_t *pJSON_EMK = NULL;
    json_t *pJSON_ESyncKey = NULL;

    // if we supply a care package, use it instead
    if (szLoginPackage)
    {
        ABC_STRDUP(szLoginPackage_JSON, szLoginPackage);
    }
    else
    {
        ABC_CHECK_ASSERT(AccountNum >= 0, ABC_CC_AccountDoesNotExist, "Bad account number");

        // get the main account directory
        ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
        ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));

        // create the name of the care package file
        ABC_ALLOC(szLoginPackageFilename, ABC_FILEIO_MAX_PATH_LENGTH);
        sprintf(szLoginPackageFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);

        // load the care package
        ABC_CHECK_RET(ABC_FileIOReadFileStr(szLoginPackageFilename, &szLoginPackage_JSON, pError));
    }

    // decode the json
    json_error_t error;
    pJSON_Root = json_loads(szLoginPackage_JSON, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing JSON login package");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON login package");

    // get the EMK
    pJSON_EMK = json_object_get(pJSON_Root, JSON_ACCT_MK_FIELD);
    ABC_CHECK_ASSERT((pJSON_EMK && json_is_object(pJSON_EMK)), ABC_CC_JSONError, "Error parsing JSON care package - missing ERQ");

    // get ESyncKey
    pJSON_ESyncKey = json_object_get(pJSON_Root, JSON_ACCT_SYNCKEY_FIELD);
    ABC_CHECK_ASSERT((pJSON_ESyncKey && json_is_object(pJSON_ESyncKey)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP2");

    if (ppJSON_EMK)
    {
        *ppJSON_EMK = json_incref(pJSON_EMK);
    }
    if (ppJSON_ESyncKey)
    {
        *ppJSON_ESyncKey = json_incref(pJSON_ESyncKey);
    }
exit:
    if (pJSON_Root)             json_decref(pJSON_Root);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szLoginPackageFilename);
    ABC_FREE_STR(szLoginPackage_JSON);

    return cc;
}

/**
 * Creates a new sync directory and all the files needed for the given account
 */
static
tABC_CC ABC_LoginCreateSync(const char *szAccountsRootDir, bool bIncludeDefs,
                              tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szDataJSON = NULL;
    char *szFilename = NULL;

    ABC_CHECK_NULL(szAccountsRootDir);

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the sync directory
    sprintf(szFilename, "%s/%s", szAccountsRootDir, ACCOUNT_SYNC_DIR);
    ABC_CHECK_RET(ABC_FileIOCreateDir(szFilename, pError));

    if (bIncludeDefs)
    {
        // create initial categories file with no entries
        ABC_CHECK_RET(ABC_LoginCreateListJSON(JSON_ACCT_CATEGORIES_FIELD, "", &szDataJSON, pError));
        sprintf(szFilename, "%s/%s/%s", szAccountsRootDir, ACCOUNT_SYNC_DIR, ACCOUNT_CATEGORIES_FILENAME);
        ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szDataJSON, pError));
        ABC_FREE_STR(szDataJSON);
        szDataJSON = NULL;
    }
exit:
    ABC_FREE_STR(szDataJSON);
    ABC_FREE_STR(szFilename);

    return cc;
}

/**
 * Finds the next available account number (the number is just used for the directory name)
 */
static
tABC_CC ABC_LoginNextAccountNum(int *pAccountNum,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountRoot = NULL;
    char *szAccountDir = NULL;

    ABC_CHECK_NULL(pAccountNum);

    // make sure the accounts directory is in place
    ABC_CHECK_RET(ABC_LoginCreateRootDir(pError));

    // get the account root directory string
    ABC_ALLOC(szAccountRoot, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(szAccountRoot, pError));

    // run through all the account names
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    int AccountNum;
    for (AccountNum = 0; AccountNum < ACCOUNT_MAX; AccountNum++)
    {
        ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));
        bool bExists = false;
        ABC_CHECK_RET(ABC_FileIOFileExists(szAccountDir, &bExists, pError));
        if (true != bExists)
        {
            break;
        }
    }

    // if we went to the end
    if (AccountNum == ACCOUNT_MAX)
    {
        ABC_RET_ERROR(ABC_CC_NoAvailAccountSpace, "No account space available");
    }

    *pAccountNum = AccountNum;

exit:
    ABC_FREE_STR(szAccountRoot);
    ABC_FREE_STR(szAccountDir);

    return cc;
}

/**
 * creates the account directory if needed
 */
static
tABC_CC ABC_LoginCreateRootDir(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountRoot = NULL;

    // create the account directory string
    ABC_ALLOC(szAccountRoot, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(szAccountRoot, pError));

    // if it doesn't exist
    bool bExists = false;
    ABC_CHECK_RET(ABC_FileIOFileExists(szAccountRoot, &bExists, pError));
    if (true != bExists)
    {
        ABC_CHECK_RET(ABC_FileIOCreateDir(szAccountRoot, pError));
    }

exit:
    ABC_FREE_STR(szAccountRoot);

    return cc;
}

/**
 * Gets the root account directory
 *
 * @param pszRootDir pointer to store allocated string
 *                   (the user is responsible for free'ing)
 */
tABC_CC ABC_LoginGetRootDir(char **pszRootDir, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_NULL(pszRootDir);

    // create the account directory string
    ABC_ALLOC(*pszRootDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(*pszRootDir, pError));

exit:

    return cc;
}

/**
 * Copies the root account directory into the string given
 *
 * @param szRootDir pointer into which to copy the string
 */
static
tABC_CC ABC_LoginCopyRootDirName(char *szRootDir, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szFileIORootDir = NULL;

    ABC_CHECK_NULL(szRootDir);

    ABC_CHECK_RET(ABC_FileIOGetRootDir(&szFileIORootDir, pError));

    // create the account directory string
    sprintf(szRootDir, "%s/%s", szFileIORootDir, ACCOUNT_DIR);

exit:
    ABC_FREE_STR(szFileIORootDir);

    return cc;
}

/**
 * Gets the account directory for a given username
 *
 * @param pszDirName Location to store allocated pointer (must be free'd by caller)
 */
tABC_CC ABC_LoginGetDirName(const char *szUserName, char **pszDirName, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szDirName = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pszDirName);

    int accountNum = -1;

    // check locally for the account
    ABC_CHECK_RET(ABC_LoginNumForUser(szUserName, &accountNum, pError));
    if (accountNum < 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
    }

    // get the account root directory string
    ABC_ALLOC(szDirName, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szDirName, accountNum, pError));
    *pszDirName = szDirName;
    szDirName = NULL; // so we don't free it


exit:
    ABC_FREE_STR(szDirName);

    return cc;
}

/**
 * Gets the account sync directory for a given username
 *
 * @param pszDirName Location to store allocated pointer (must be free'd by caller)
 */
tABC_CC ABC_LoginGetSyncDirName(const char *szUserName,
                                  char **pszDirName,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szDirName = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pszDirName);

    ABC_CHECK_RET(ABC_LoginGetDirName(szUserName, &szDirName, pError));

    ABC_ALLOC(*pszDirName, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(*pszDirName, "%s/%s", szDirName, ACCOUNT_SYNC_DIR);

exit:
    ABC_FREE_STR(szDirName);

    return cc;
}

/*
 * Copies the account directory name into the string given
 */
static
tABC_CC ABC_LoginCopyAccountDirName(char *szAccountDir, int AccountNum, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountRoot = NULL;

    ABC_CHECK_NULL(szAccountDir);

    // get the account root directory string
    ABC_ALLOC(szAccountRoot, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(szAccountRoot, pError));

    // create the account directory string
    sprintf(szAccountDir, "%s/%s%d", szAccountRoot, ACCOUNT_FOLDER_PREFIX, AccountNum);

exit:
    ABC_FREE_STR(szAccountRoot);

    return cc;
}

/**
 * creates the json for a list of items in a string seperated by newlines
 * for example:
 *   "A\nB\n"
 * becomes
 *  { "name" : [ "A", "B" ] }
 */
static
tABC_CC ABC_LoginCreateListJSON(const char *szName, const char *szItems, char **pszJSON,  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *jsonItems = NULL;
    json_t *jsonItemArray = NULL;
    char *szNewItems = NULL;

    ABC_CHECK_NULL(szName);
    ABC_CHECK_NULL(szItems);
    ABC_CHECK_NULL(pszJSON);

    // create the json object that will be our questions
    jsonItems = json_object();
    jsonItemArray = json_array();

    if (strlen(szItems))
    {
        // change all the newlines into nulls to create a string of them
        ABC_STRDUP(szNewItems, szItems);
        int nItems = 1;
        for (int i = 0; i < strlen(szItems); i++)
        {
            if (szNewItems[i] == '\n')
            {
                nItems++;
                szNewItems[i] = '\0';
            }
        }

        // for each item
        char *pCurItem = szNewItems;
        for (int i = 0; i < nItems; i++)
        {
            json_array_append_new(jsonItemArray, json_string(pCurItem));
            pCurItem += strlen(pCurItem) + 1;
        }
    }

    // set our final json for the questions
    json_object_set(jsonItems, szName, jsonItemArray);

    *pszJSON = ABC_UtilStringFromJSONObject(jsonItems, JSON_INDENT(4) | JSON_PRESERVE_ORDER);

exit:
    if (jsonItems)      json_decref(jsonItems);
    if (jsonItemArray)  json_decref(jsonItemArray);
    ABC_FREE_STR(szNewItems);

    return cc;
}


/*
 * returns the account number associated with the given user name
 * -1 is returned if the account does not exist
 */
static
tABC_CC ABC_LoginNumForUser(const char *szUserName, int *pAccountNum, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szCurUserName = NULL;
    char *szAccountRoot = NULL;
    tABC_FileIOList *pFileList = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pAccountNum);

    // assume we didn't find it
    *pAccountNum = -1;

    // make sure the accounts directory is in place
    ABC_CHECK_RET(ABC_LoginCreateRootDir(pError));

    // get the account root directory string
    ABC_ALLOC(szAccountRoot, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(szAccountRoot, pError));

    // get all the files in this root

    ABC_FileIOCreateFileList(&pFileList, szAccountRoot, NULL);
    for (int i = 0; i < pFileList->nCount; i++)
    {
        // if this file is a directory
        if (pFileList->apFiles[i]->type == ABC_FILEIOFileType_Directory)
        {
            // if this directory starts with the right prefix
            if ((strlen(pFileList->apFiles[i]->szName) > strlen(ACCOUNT_FOLDER_PREFIX)) &&
                (strncmp(ACCOUNT_FOLDER_PREFIX, pFileList->apFiles[i]->szName, strlen(ACCOUNT_FOLDER_PREFIX)) == 0))
            {
                char *szAccountNum = (char *)(pFileList->apFiles[i]->szName + strlen(ACCOUNT_FOLDER_PREFIX));
                unsigned int AccountNum = (unsigned int) strtol(szAccountNum, NULL, 10); // 10 is for base-10

                // get the username for this account
                ABC_CHECK_RET(ABC_LoginUserForNum(AccountNum, &szCurUserName, pError));

                // if this matches what we are looking for
                if (strcmp(szUserName, szCurUserName) == 0)
                {
                    *pAccountNum = AccountNum;
                    break;
                }
                ABC_FREE_STR(szCurUserName);
                szCurUserName = NULL;
            }
        }
    }


exit:
    ABC_FREE_STR(szCurUserName);
    ABC_FREE_STR(szAccountRoot);
    ABC_FileIOFreeFileList(pFileList);

    return cc;
}

/**
 * Gets the user name for the specified account number
 *
 * @param pszUserName Location to store allocated pointer (must be free'd by caller)
 */
static
tABC_CC ABC_LoginUserForNum(unsigned int AccountNum, char **pszUserName, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *root = NULL;
    char *szAccountNameJSON = NULL;
    char *szAccountRoot = NULL;
    char *szAccountNamePath = NULL;

    ABC_CHECK_NULL(pszUserName);

    // make sure the accounts directory is in place
    ABC_CHECK_RET(ABC_LoginCreateRootDir(pError));

    // get the account root directory string
    ABC_ALLOC(szAccountRoot, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyRootDirName(szAccountRoot, pError));

    // create the path to the account name file
    ABC_ALLOC(szAccountNamePath, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szAccountNamePath, "%s/%s%d/%s", szAccountRoot, ACCOUNT_FOLDER_PREFIX, AccountNum, ACCOUNT_NAME_FILENAME);

    // read in the json
    ABC_CHECK_RET(ABC_FileIOReadFileStr(szAccountNamePath, &szAccountNameJSON, pError));

    // parse out the user name
    json_error_t error;
    root = json_loads(szAccountNameJSON, 0, &error);
    ABC_CHECK_ASSERT(root != NULL, ABC_CC_JSONError, "Error parsing JSON account name");
    ABC_CHECK_ASSERT(json_is_object(root), ABC_CC_JSONError, "Error parsing JSON account name");

    json_t *jsonVal = json_object_get(root, JSON_ACCT_USERNAME_FIELD);
    ABC_CHECK_ASSERT((jsonVal && json_is_string(jsonVal)), ABC_CC_JSONError, "Error parsing JSON account name");
    const char *szUserName = json_string_value(jsonVal);

    ABC_STRDUP(*pszUserName, szUserName);

exit:
    if (root)               json_decref(root);
    ABC_FREE_STR(szAccountNameJSON);
    ABC_FREE_STR(szAccountRoot);
    ABC_FREE_STR(szAccountNamePath);

    return cc;
}

/**
 * Clears all the keys from the cache
 */
tABC_CC ABC_LoginClearKeyCache(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));

    if ((gAccountKeysCacheCount > 0) && (NULL != gaAccountKeysCacheArray))
    {
        for (int i = 0; i < gAccountKeysCacheCount; i++)
        {
            tAccountKeys *pAccountKeys = gaAccountKeysCacheArray[i];
            ABC_LoginFreeAccountKeys(pAccountKeys);
        }

        ABC_FREE(gaAccountKeysCacheArray);
        gAccountKeysCacheCount = 0;
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Frees all the elements in the given AccountKeys struct
 */
static void ABC_LoginFreeAccountKeys(tAccountKeys *pAccountKeys)
{
    if (pAccountKeys)
    {
        ABC_FREE_STR(pAccountKeys->szUserName);

        ABC_FREE_STR(pAccountKeys->szPassword);

        ABC_FREE_STR(pAccountKeys->szPIN);
        ABC_FREE_STR(pAccountKeys->szRepoAcctKey);

        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP1));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP2));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP3));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP4));

        ABC_BUF_FREE(pAccountKeys->L);
        ABC_BUF_FREE(pAccountKeys->L1);
        ABC_BUF_FREE(pAccountKeys->P);
        ABC_BUF_FREE(pAccountKeys->P1);
        ABC_BUF_FREE(pAccountKeys->LRA);
        ABC_BUF_FREE(pAccountKeys->LRA1);
        ABC_BUF_FREE(pAccountKeys->L2);
        ABC_BUF_FREE(pAccountKeys->RQ);
        ABC_BUF_FREE(pAccountKeys->LP);
        ABC_BUF_FREE(pAccountKeys->LP2);
        ABC_BUF_FREE(pAccountKeys->LRA2);
    }
}

/**
 * Adds the given AccountKey to the array of cached account keys
 */
static tABC_CC ABC_LoginAddToKeyCache(tAccountKeys *pAccountKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(pAccountKeys);

    // see if it exists first
    tAccountKeys *pExistingAccountKeys = NULL;
    ABC_CHECK_RET(ABC_LoginKeyFromCacheByName(pAccountKeys->szUserName, &pExistingAccountKeys, pError));

    // if it doesn't currently exist in the array
    if (pExistingAccountKeys == NULL)
    {
        // if we don't have an array yet
        if ((gAccountKeysCacheCount == 0) || (NULL == gaAccountKeysCacheArray))
        {
            // create a new one
            gAccountKeysCacheCount = 0;
            ABC_ALLOC(gaAccountKeysCacheArray, sizeof(tAccountKeys *));
        }
        else
        {
            // extend the current one
            gaAccountKeysCacheArray = realloc(gaAccountKeysCacheArray, sizeof(tAccountKeys *) * (gAccountKeysCacheCount + 1));

        }
        gaAccountKeysCacheArray[gAccountKeysCacheCount] = pAccountKeys;
        gAccountKeysCacheCount++;
    }
    else
    {
        cc = ABC_CC_AccountAlreadyExists;
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Searches for a key in the cached by account name
 * if it is not found, the account keys will be set to NULL
 */
static tABC_CC ABC_LoginKeyFromCacheByName(const char *szUserName, tAccountKeys **ppAccountKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(ppAccountKeys);

    // assume we didn't find it
    *ppAccountKeys = NULL;

    if ((gAccountKeysCacheCount > 0) && (NULL != gaAccountKeysCacheArray))
    {
        for (int i = 0; i < gAccountKeysCacheCount; i++)
        {
            tAccountKeys *pAccountKeys = gaAccountKeysCacheArray[i];
            if (0 == strcmp(szUserName, pAccountKeys->szUserName))
            {
                // found it
                *ppAccountKeys = pAccountKeys;
                break;
            }
        }
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Adds the given user to the key cache if it isn't already cached.
 * With or without a password, szUserName, L, SNRP1, SNRP2, SNRP3, SNRP4 keys are retrieved and added if they aren't already in the cache
 * If a password is given, szPassword, szPIN, P, LP2 keys are retrieved and the entry is added
 *  (the initial keys are added so the password can be verified while trying to
 *  decrypt the settings files)
 * If a pointer to hold the keys is given, then it is set to those keys
 */
static
tABC_CC ABC_LoginCacheKeys(const char *szUserName, const char *szPassword, tAccountKeys **ppKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys              = NULL;
    tAccountKeys *pFinalKeys         = NULL;
    json_t       *pJSON_SNRP2        = NULL;
    json_t       *pJSON_SNRP3        = NULL;
    json_t       *pJSON_SNRP4        = NULL;
    json_t       *pJSON_EMK          = NULL;
    json_t       *pJSON_ESyncKey     = NULL;
    tABC_U08Buf  PIN_JSON            = ABC_BUF_NULL;
    tABC_U08Buf  REPO_JSON           = ABC_BUF_NULL;
    tABC_U08Buf  MK                  = ABC_BUF_NULL;
    json_t       *pJSON_Root         = NULL;
    tABC_U08Buf  P                   = ABC_BUF_NULL;
    tABC_U08Buf  LP                  = ABC_BUF_NULL;
    tABC_U08Buf  LP2                 = ABC_BUF_NULL;
    tABC_LoginSettings    *pSettings = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);

    // see if it is already in the cache
    ABC_CHECK_RET(ABC_LoginKeyFromCacheByName(szUserName, &pFinalKeys, pError));

    // if there wasn't an entry already in the cache - let's add it
    if (NULL == pFinalKeys)
    {
        // we need to add it but start with only those things that require the user name

        // check if the account exists
        int AccountNum = -1;
        ABC_CHECK_RET(ABC_LoginNumForUser(szUserName, &AccountNum, pError));
        if (AccountNum >= 0)
        {
            ABC_ALLOC(pKeys, sizeof(tAccountKeys));
            pKeys->accountNum = AccountNum;
            ABC_STRDUP(pKeys->szUserName, szUserName);
            pKeys->szPassword = NULL;

            ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(AccountNum, NULL, NULL, &pJSON_SNRP2, &pJSON_SNRP3, &pJSON_SNRP4, pError));

            // SNRP's
            ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP2, &(pKeys->pSNRP2), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP3, &(pKeys->pSNRP3), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP4, &(pKeys->pSNRP4), pError));

            // L = username
            ABC_BUF_DUP_PTR(pKeys->L, pKeys->szUserName, strlen(pKeys->szUserName));

            // L1 = scrypt(L, SNRP1)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));

            // L2 = scrypt(L, SNRP4)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L2), pError));

            // add new starting account keys to the key cache
            ABC_CHECK_RET(ABC_LoginAddToKeyCache(pKeys, pError));
            pFinalKeys = pKeys;
            pKeys = NULL; // so we don't free it at the end

        }
        else
        {
            ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
        }
    }

    // at this point there is now one in the cache and it is pFinalKeys
    // but it may or may not have password keys

    // Fetch login package objects
    ABC_CHECK_RET(
        ABC_LoginGetLoginPackageObjects(
            pFinalKeys->accountNum, NULL,
            &pJSON_EMK, &pJSON_ESyncKey, pError));

    // try to decrypt RepoAcctKey
    if (pJSON_ESyncKey)
    {
        tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONObject(pJSON_ESyncKey, pFinalKeys->L2, &REPO_JSON, pError);

        // check the results
        if (ABC_CC_DecryptFailure == CC_Decrypt)
        {
            ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt RepoAcctKey - bad L2");
        }
        else if (ABC_CC_Ok != CC_Decrypt)
        {
            cc = CC_Decrypt;
            goto exit;
        }

        tABC_U08Buf FinalSyncKey;
        ABC_BUF_DUP(FinalSyncKey, REPO_JSON);
        ABC_BUF_APPEND_PTR(FinalSyncKey, "", 1);
        pFinalKeys->szRepoAcctKey = (char*) ABC_BUF_PTR(FinalSyncKey);
        ABC_DebugLog("%s\n", pFinalKeys->szRepoAcctKey);
    }
    else
    {
        pFinalKeys->szRepoAcctKey = NULL;
    }

    // if we are given a password
    if (NULL != szPassword)
    {
        // if there is no key in the cache, let's add the keys we can with a password
        if (NULL == pFinalKeys->szPassword)
        {
            // P = password
            ABC_BUF_DUP_PTR(P, szPassword, strlen(szPassword));

            // LP = L + P
            ABC_BUF_DUP(LP, pFinalKeys->L);
            ABC_BUF_APPEND(LP, P);

            // LP2 = Scrypt(L + P, SNRP2)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(LP, pFinalKeys->pSNRP2, &LP2, pError));

            // try to decrypt MK
            tABC_CC CC_Decrypt =
                ABC_CryptoDecryptJSONObject(pJSON_EMK, LP2, &MK, pError);

            // check the results
            if (ABC_CC_DecryptFailure == CC_Decrypt)
            {
                // the assumption here is that this specific error is due to a bad password
                ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt PIN - bad password");
            }
            else if (ABC_CC_Ok != CC_Decrypt)
            {
                // this was an error other than just a bad key so we need to treat this like an error
                cc = CC_Decrypt;
                goto exit;
            }


            // if we got here, then the password was good so we can add what we just calculated to the keys
            ABC_STRDUP(pFinalKeys->szPassword, szPassword);
            ABC_BUF_SET(pFinalKeys->MK, MK);
            ABC_BUF_CLEAR(MK);
            ABC_BUF_SET(pFinalKeys->P, P);
            ABC_BUF_CLEAR(P);
            ABC_BUF_SET(pFinalKeys->LP, LP);
            ABC_BUF_CLEAR(LP);
            ABC_BUF_SET(pFinalKeys->LP2, LP2);
            ABC_BUF_CLEAR(LP2);

            ABC_CHECK_RET(ABC_LoadAccountSettings(szUserName, szPassword, &pSettings, pError));
            if (pSettings)
            {
                ABC_STRDUP(pFinalKeys->szPIN, pSettings->szPIN);
            }
        }
        else
        {
            // make sure it is correct
            if (0 != strcmp(pFinalKeys->szPassword, szPassword))
            {
                ABC_RET_ERROR(ABC_CC_BadPassword, "Password is incorrect");
            }
        }
    }

    // if they wanted the keys
    if (ppKeys)
    {
        *ppKeys = pFinalKeys;
    }

exit:
    if (pKeys)
    {
        ABC_LoginFreeAccountKeys(pKeys);
        ABC_CLEAR_FREE(pKeys, sizeof(tAccountKeys));
    }
    ABC_LoginFreeSettings(pSettings);
    if (pJSON_SNRP2)    json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)    json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)    json_decref(pJSON_SNRP4);
    if (pJSON_EMK)      json_decref(pJSON_EMK);
    if (pJSON_ESyncKey) json_decref(pJSON_ESyncKey);
    if (pJSON_Root)     json_decref(pJSON_Root);
    ABC_BUF_FREE(PIN_JSON);
    ABC_BUF_FREE(P);
    ABC_BUF_FREE(LP);
    ABC_BUF_FREE(LP2);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Retrieves the specified key from the key cache
 * if the account associated with the username and password is not currently in the cache, it is added
 */
tABC_CC ABC_LoginGetKey(const char *szUserName, const char *szPassword, tABC_LoginKey keyType, tABC_U08Buf *pKey, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys = NULL;
    json_t       *pJSON_ERQ     = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pKey);

    // make sure the account is in the cache
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));
    ABC_CHECK_ASSERT(NULL != pKeys, ABC_CC_Error, "Expected to find account keys in cache.");

    switch(keyType)
    {
        case ABC_LoginKey_L1:
            // L1 = Scrypt(L, SNRP1)
            if (NULL == ABC_BUF_PTR(pKeys->L1))
            {
                ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
                ABC_CHECK_ASSERT(NULL != pKeys->pSNRP1, ABC_CC_Error, "Expected to find SNRP1 in key cache");
                ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
            }
            ABC_BUF_SET(*pKey, pKeys->L1);
            break;

        case ABC_LoginKey_L2:
            // L2 = Scrypt(L, SNRP4)
            if (NULL == ABC_BUF_PTR(pKeys->L2))
            {
                ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
                ABC_CHECK_ASSERT(NULL != pKeys->pSNRP4, ABC_CC_Error, "Expected to find SNRP4 in key cache");
                ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L2), pError));
            }
            ABC_BUF_SET(*pKey, pKeys->L2);
            break;

        case ABC_LoginKey_LP2:
            // this should already be in the cache
            ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP2), ABC_CC_Error, "Expected to find LP2 in key cache");
            ABC_BUF_SET(*pKey, pKeys->LP2);
            break;

        case ABC_LoginKey_PIN:
            // this should already be in the cache
            ABC_CHECK_ASSERT(NULL != pKeys->szPIN, ABC_CC_Error, "Expected to find PIN in key cache");
            ABC_BUF_SET_PTR(*pKey, (unsigned char *)pKeys->szPIN, sizeof(pKeys->szPIN) + 1);
            break;

        case ABC_LoginKey_RepoAccountKey:
            // this should already be in the cache
            if (NULL == pKeys->szRepoAcctKey)
            {
            }
            ABC_BUF_SET_PTR(*pKey, (unsigned char *)pKeys->szRepoAcctKey, sizeof(pKeys->szRepoAcctKey) + 1);
            break;

        case ABC_LoginKey_RQ:
            // RQ - if ERQ available
            if (NULL == ABC_BUF_PTR(pKeys->RQ))
            {
                // get L2
                tABC_U08Buf L2;
                ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_L2, &L2, pError));

                // get ERQ
                int AccountNum = -1;
                ABC_CHECK_RET(ABC_LoginNumForUser(szUserName, &AccountNum, pError));
                ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(AccountNum, NULL, &pJSON_ERQ, NULL, NULL, NULL, pError));

                // RQ - if ERQ available
                if (pJSON_ERQ != NULL)
                {
                    ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ERQ, L2, &(pKeys->RQ), pError));
                }
                else
                {
                    ABC_RET_ERROR(ABC_CC_NoRecoveryQuestions, "There are no recovery questions for this user");
                }
            }
            ABC_BUF_SET(*pKey, pKeys->RQ);
            break;

        default:
            ABC_RET_ERROR(ABC_CC_Error, "Unknown key type");
            break;

    };

exit:
    if (pJSON_ERQ)  json_decref(pJSON_ERQ);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Sets the PIN for the given account
 *
 * @param szPIN PIN to use for the account
 */
tABC_CC ABC_LoginSetPIN(const char *szUserName,
                          const char *szPassword,
                          const char *szPIN,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    int                  dirty;
    tAccountKeys         *pKeys     = NULL;
    tABC_LoginSettings *pSettings = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szPassword);
    ABC_CHECK_NULL(szPIN);

    ABC_CHECK_RET(ABC_LoginLoadSettings(szUserName, szPassword, &pSettings, pError));

    // get the key cache
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));

    // set the new PIN
    ABC_FREE_STR(pKeys->szPIN);
    ABC_STRDUP(pKeys->szPIN, szPIN);

    ABC_FREE_STR(pSettings->szPIN);
    ABC_STRDUP(pSettings->szPIN, szPIN);

    ABC_CHECK_RET(ABC_LoginSaveSettings(szUserName, szPassword, pSettings, pError));
    ABC_CHECK_RET(ABC_LoginSyncData(szUserName, szPassword, &dirty, pError));
exit:
    ABC_LoginFreeSettings(pSettings);

    return cc;
}

/**
 * This function gets the categories for an account.
 * An array of allocated strings is allocated so the user is responsible for
 * free'ing all the elements as well as the array itself.
 */
tABC_CC ABC_LoginGetCategories(const char *szUserName,
                                 char ***paszCategories,
                                 unsigned int *pCount,
                                 tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountDir = NULL;
    char *szFilename = NULL;
    char *szJSON = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(paszCategories);
    *paszCategories = NULL;
    ABC_CHECK_NULL(pCount);
    *pCount = 0;

    // load the categories
    ABC_CHECK_RET(ABC_LoginGetDirName(szUserName, &szAccountDir, pError));
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_CATEGORIES_FILENAME);
    ABC_CHECK_RET(ABC_FileIOReadFileStr(szFilename, &szJSON, pError));

    // load the strings of values
    ABC_CHECK_RET(ABC_UtilGetArrayValuesFromJSONString(szJSON, JSON_ACCT_CATEGORIES_FIELD, paszCategories, pCount, pError));

exit:
    ABC_FREE_STR(szJSON);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);

    return cc;
}

/**
 * This function adds a category to an account.
 * No attempt is made to avoid a duplicate entry.
 */
tABC_CC ABC_LoginAddCategory(const char *szUserName,
                               char *szCategory,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char **aszCategories = NULL;
    unsigned int categoryCount = 0;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szCategory);

    // load the current categories
    ABC_CHECK_RET(ABC_LoginGetCategories(szUserName, &aszCategories, &categoryCount, pError));

    // if there are categories
    if ((aszCategories != NULL) && (categoryCount > 0))
    {
        aszCategories = realloc(aszCategories, sizeof(char *) * (categoryCount + 1));
    }
    else
    {
        ABC_ALLOC(aszCategories, sizeof(char *));
        categoryCount = 0;
    }
    ABC_STRDUP(aszCategories[categoryCount], szCategory);
    categoryCount++;

    // save out the categories
    ABC_CHECK_RET(ABC_LoginSaveCategories(szUserName, aszCategories, categoryCount, pError));

exit:
    ABC_UtilFreeStringArray(aszCategories, categoryCount);

    return cc;
}

/**
 * This function removes a category from an account.
 * If there is more than one category with this name, all categories by this name are removed.
 * If the category does not exist, no error is returned.
 */
tABC_CC ABC_LoginRemoveCategory(const char *szUserName,
                                  char *szCategory,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char **aszCategories = NULL;
    unsigned int categoryCount = 0;
    char **aszNewCategories = NULL;
    unsigned int newCategoryCount = 0;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szCategory);

    // load the current categories
    ABC_CHECK_RET(ABC_LoginGetCategories(szUserName, &aszCategories, &categoryCount, pError));

    // got through all the categories and only add ones that are not this one
    for (int i = 0; i < categoryCount; i++)
    {
        // if this is not the string we are looking to remove then add it to our new arrary
        if (0 != strcmp(aszCategories[i], szCategory))
        {
            // if there are categories
            if ((aszNewCategories != NULL) && (newCategoryCount > 0))
            {
                aszNewCategories = realloc(aszNewCategories, sizeof(char *) * (newCategoryCount + 1));
            }
            else
            {
                ABC_ALLOC(aszNewCategories, sizeof(char *));
                newCategoryCount = 0;
            }
            ABC_STRDUP(aszNewCategories[newCategoryCount], aszCategories[i]);
            newCategoryCount++;
        }
    }

    // save out the new categories
    ABC_CHECK_RET(ABC_LoginSaveCategories(szUserName, aszNewCategories, newCategoryCount, pError));

exit:
    ABC_UtilFreeStringArray(aszCategories, categoryCount);
    ABC_UtilFreeStringArray(aszNewCategories, newCategoryCount);

    return cc;
}

/**
 * Saves the categories for the given account
 */
static
tABC_CC ABC_LoginSaveCategories(const char *szUserName,
                                  char **aszCategories,
                                  unsigned int count,
                                  tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szDataJSON = NULL;
    char *szFilename = NULL;
    char *szAccountDir = NULL;

    ABC_CHECK_NULL(szUserName);

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the categories JSON
    ABC_CHECK_RET(ABC_UtilCreateArrayJSONString(aszCategories, count, JSON_ACCT_CATEGORIES_FIELD, &szDataJSON, pError));

    // write them out
    ABC_CHECK_RET(ABC_LoginGetDirName(szUserName, &szAccountDir, pError));
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s/%s", szAccountDir, ACCOUNT_SYNC_DIR, ACCOUNT_CATEGORIES_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szDataJSON, pError));

exit:
    ABC_FREE_STR(szDataJSON);
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szAccountDir);

    return cc;
}

/**
 * Check that the recovery answers for a given account are valid
 * @param pbValid true is stored in here if they are correct
 */
tABC_CC ABC_LoginCheckRecoveryAnswers(const char *szUserName,
                                        const char *szRecoveryAnswers,
                                        bool *pbValid,
                                        tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tAccountKeys *pKeys    = NULL;
    tABC_U08Buf LRA        = ABC_BUF_NULL;
    tABC_U08Buf LRA2       = ABC_BUF_NULL;
    tABC_U08Buf LRA1       = ABC_BUF_NULL;
    tABC_U08Buf LP2        = ABC_BUF_NULL;
    char *szAccountDir     = NULL;
    char *szAccountSyncDir = NULL;
    char *szFilename       = NULL;
    bool bExists           = false;

    // Use for the remote answers check
    tABC_U08Buf L           = ABC_BUF_NULL;
    tABC_U08Buf L1          = ABC_BUF_NULL;
    tABC_U08Buf L2          = ABC_BUF_NULL;
    tABC_U08Buf P1_NULL     = ABC_BUF_NULL;
    char *szERepoAcctKey    = NULL;
    tABC_CryptoSNRP *pSNRP1 = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szRecoveryAnswers);
    ABC_CHECK_NULL(pbValid);
    *pbValid = false;

    // If we have the care package cached, check recovery answers remotely
    // and if successful, setup the account locally
    if (gCarePackageCache)
    {
        ABC_BUF_DUP_PTR(L, szUserName, strlen(szUserName));

        ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));

        ABC_BUF_DUP(LRA, L);
        ABC_BUF_APPEND_PTR(LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pSNRP1, &LRA1, pError));

        // Fetch ERepoAcctKey using P1 or LRA1, if successful, szRecoveryAnswers are correct
        ABC_CHECK_RET(ABC_LoginServerGetRepoAcctKey(L1, P1_NULL, LRA1, &szERepoAcctKey, pError));

        // Setup initial account and set the care package
        ABC_CHECK_RET(ABC_LoginFetchInitCarePackage(szUserName, L1, gCarePackageCache, &szAccountDir, pError));
        ABC_FREE_STR(gCarePackageCache);
        gCarePackageCache = NULL;

        // We have the care package so fetch keys without password
        ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, NULL, &pKeys, pError));

        // L2 = Scrypt(L, SNRP4)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pKeys->pSNRP4, &L2, pError));

        // Fetch the ERepoAcctKey
        ABC_CHECK_RET(ABC_LoginFetchRepoKey(L1, P1_NULL, LRA1, szAccountDir, pError));

        // Setup the account repo and sync
        ABC_CHECK_RET(ABC_LoginRepoSetup(szUserName, szAccountDir, pError));
    }

    // pull this account into the cache
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, NULL, &pKeys, pError));

    // create our LRA (L + RA) with the answers given
    ABC_BUF_DUP(LRA, pKeys->L);
    ABC_BUF_APPEND_PTR(LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));

    // if the cache has an LRA
    if (ABC_BUF_PTR(pKeys->LRA) != NULL)
    {
        // check if they are equal
        if (ABC_BUF_SIZE(LRA) == ABC_BUF_SIZE(pKeys->LRA))
        {
            if (0 == memcmp(ABC_BUF_PTR(LRA), ABC_BUF_PTR(pKeys->LRA), ABC_BUF_SIZE(LRA)))
            {
                *pbValid = true;
            }
        }
    }
    else
    {
        // we will need to attempt to decrypt ELP2 in order to determine whether we have the right LRA
        // ELP2.json <- LP2 encrypted with recovery key (LRA2)

        // create our LRA2 = Scrypt(L + RA, SNRP3)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP3, &LRA2, pError));

        // create our LRA1 = Scrypt(L + RA, SNRP1)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP1, &LRA1, pError));

        ABC_CHECK_RET(ABC_LoginGetSyncDirName(szUserName, &szAccountSyncDir, pError));
        ABC_CHECK_RET(ABC_FileIOFileExists(szAccountSyncDir, &bExists, pError));

        // attempt to decode ELP2
        ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
        sprintf(szFilename, "%s/%s", szAccountSyncDir, ACCOUNT_ELP2_FILENAME);
        tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONFile(szFilename, LRA2, &LP2, pError);

        // check the results
        if (ABC_CC_Ok == CC_Decrypt)
        {
            *pbValid = true;
        }
        else if (ABC_CC_DecryptFailure != CC_Decrypt)
        {
            // this was an error other than just a bad key so we need to treat this like an error
            cc = CC_Decrypt;
            goto exit;
        }
        else
        {
            // clear the error because we know why it failed and we will set that in the pbValid
            ABC_SET_ERR_CODE(pError, ABC_CC_Ok);
        }

        // if we were successful, save our keys in the cache since we spent the time to create them
        if (*pbValid == true)
        {
            ABC_BUF_SET(pKeys->LRA, LRA);
            ABC_BUF_CLEAR(LRA); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LRA2, LRA2);
            ABC_BUF_CLEAR(LRA2); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LRA1, LRA1);
            ABC_BUF_CLEAR(LRA1); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LP2, LP2);
            ABC_BUF_CLEAR(LP2); // so we don't free as we exit
        }
    }
exit:
    ABC_BUF_FREE(LRA);
    ABC_BUF_FREE(LRA2);
    ABC_BUF_FREE(LRA1);
    ABC_BUF_FREE(LP2);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szAccountSyncDir);
    ABC_FREE_STR(szFilename);

    ABC_BUF_FREE(L1);
    ABC_FREE_STR(szERepoAcctKey);
    ABC_CryptoFreeSNRP(&pSNRP1);

    return cc;
}

static
tABC_CC ABC_LoginServerGetCarePackage(tABC_U08Buf L1, char **szCarePackage, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szURL = NULL;
    tABC_U08Buf P1 = ABC_BUF_NULL;
    tABC_U08Buf LRA1 = ABC_BUF_NULL;

    ABC_CHECK_NULL_BUF(L1);

    // create the URL
    ABC_ALLOC(szURL, ABC_URL_MAX_PATH_LENGTH);
    sprintf(szURL, "%s/%s", ABC_SERVER_ROOT, ABC_SERVER_GET_CARE_PACKAGE_PATH);

    ABC_CHECK_RET(ABC_LoginServerGetString(L1, P1, LRA1, szURL, JSON_ACCT_CARE_PACKAGE, szCarePackage, pError));
exit:

    ABC_FREE_STR(szURL);

    return cc;
}

static
tABC_CC ABC_LoginServerGetRepoAcctKey(tABC_U08Buf L1,
                                        tABC_U08Buf P1,
                                        tABC_U08Buf LRA1,
                                        char **szERepoAcctKey,
                                        tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szURL = NULL;

    ABC_CHECK_NULL_BUF(L1);

    ABC_ALLOC(szURL, ABC_URL_MAX_PATH_LENGTH);
    sprintf(szURL, "%s/%s", ABC_SERVER_ROOT, ABC_SERVER_REPO_GET_PATH);

    ABC_CHECK_RET(ABC_LoginServerGetString(L1, P1, LRA1, szURL, JSON_ACCT_EREPO_ACCOUNT_FIELD, szERepoAcctKey, pError));
exit:

    ABC_FREE_STR(szURL);

    return cc;
}

static
tABC_CC ABC_LoginServerGetString(tABC_U08Buf L1, tABC_U08Buf P1, tABC_U08Buf LRA1,
                                   char *szURL, char *szField, char **szResponse, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t  *pJSON_Root     = NULL;
    char    *szPost         = NULL;
    char    *szL1_Base64    = NULL;
    char    *szP1_Base64    = NULL;
    char    *szResults      = NULL;

    ABC_CHECK_NULL_BUF(L1);

    // create base64 versions of L1
    ABC_CHECK_RET(ABC_CryptoBase64Encode(L1, &szL1_Base64, pError));

    // create the post data with or without P1
    if (ABC_BUF_PTR(P1) == NULL && ABC_BUF_PTR(LRA1) == NULL)
    {
        pJSON_Root = json_pack("{ss}", ABC_SERVER_JSON_L1_FIELD, szL1_Base64);
    }
    else
    {
        if (ABC_BUF_PTR(P1) == NULL)
        {
            ABC_CHECK_RET(ABC_CryptoBase64Encode(LRA1, &szP1_Base64, pError));
            pJSON_Root = json_pack("{ssss}", ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                                             ABC_SERVER_JSON_LRA1_FIELD, szP1_Base64);
        }
        else
        {
            ABC_CHECK_RET(ABC_CryptoBase64Encode(P1, &szP1_Base64, pError));
            pJSON_Root = json_pack("{ssss}", ABC_SERVER_JSON_L1_FIELD, szL1_Base64,
                                            ABC_SERVER_JSON_P1_FIELD, szP1_Base64);
        }
    }
    szPost = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_COMPACT);
    json_decref(pJSON_Root);
    pJSON_Root = NULL;
    ABC_DebugLog("Server URL: %s, Data: %s", szURL, szPost);

    // send the command
    ABC_CHECK_RET(ABC_URLPostString(szURL, szPost, &szResults, pError));
    ABC_DebugLog("Server results: %s", szResults);

    // decode the result
    json_t *pJSON_Value = NULL;
    json_error_t error;
    pJSON_Root = json_loads(szResults, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing server JSON");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON");

    // get the status code
    pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_STATUS_CODE_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_number(pJSON_Value)), ABC_CC_JSONError, "Error parsing server JSON status code");
    int statusCode = (int) json_integer_value(pJSON_Value);

    // if there was a failure
    if (ABC_Server_Code_Success != statusCode)
    {
        if (ABC_Server_Code_NoAccount == statusCode)
        {
            ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "Account does not exist on server");
        }
        else
        {
            // get the message
            pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_MESSAGE_FIELD);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_DebugLog("Server message: %s", json_string_value(pJSON_Value));
            ABC_RET_ERROR(ABC_CC_ServerError, json_string_value(pJSON_Value));
        }
    }

    // get the care package
    pJSON_Value = json_object_get(pJSON_Root, ABC_SERVER_JSON_RESULTS_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_object(pJSON_Value)), ABC_CC_JSONError, "Error parsing server JSON care package results");

    pJSON_Value = json_object_get(pJSON_Value, szField);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error care package JSON results");
    ABC_STRDUP(*szResponse, json_string_value(pJSON_Value));
exit:

    if (pJSON_Root)     json_decref(pJSON_Root);
    ABC_FREE_STR(szPost);
    ABC_FREE_STR(szL1_Base64);
    ABC_FREE_STR(szResults);

    return cc;
}

tABC_CC ABC_LoginFetchRecoveryQuestions(const char *szUserName, char **szRecoveryQuestions, tABC_Error *pError)
{
    ABC_DebugLog("%s called", __FUNCTION__);

    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    char *szAccountDir           = NULL;
    char *szCarePackage          = NULL;
    tABC_U08Buf L                = ABC_BUF_NULL;
    tABC_U08Buf L1               = ABC_BUF_NULL;
    tABC_U08Buf L2               = ABC_BUF_NULL;
    tABC_U08Buf RQ               = ABC_BUF_NULL;
    tABC_CryptoSNRP *pSNRP1      = NULL;
    tABC_CryptoSNRP *pSNRP4      = NULL;
    json_t          *pJSON_ERQ   = NULL;
    json_t          *pJSON_SNRP4 = NULL;

    // Create L, SNRP1, L1
    ABC_BUF_DUP_PTR(L, szUserName, strlen(szUserName));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));

    //  Download CarePackage.json
    ABC_CHECK_RET(ABC_LoginServerGetCarePackage(L1, &szCarePackage, pError));

    // Set the CarePackage cache
    ABC_STRDUP(gCarePackageCache, szCarePackage);

    // get ERQ and SNRP4
    ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(0, gCarePackageCache, &pJSON_ERQ, NULL, NULL, &pJSON_SNRP4, pError));

    // Create L2
    ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP4, &pSNRP4, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP4, &L2, pError));

    // RQ - if ERQ available
    if (pJSON_ERQ != NULL)
    {
        ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ERQ, L2, &RQ, pError));
    }
    else
    {
        ABC_RET_ERROR(ABC_CC_NoRecoveryQuestions, "There are no recovery questions for this user");
    }

    tABC_U08Buf FinalRQ;
    ABC_BUF_DUP(FinalRQ, RQ);
    ABC_BUF_APPEND_PTR(FinalRQ, "", 1);
    *szRecoveryQuestions = (char *) ABC_BUF_PTR(FinalRQ);
exit:
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szCarePackage);
    ABC_BUF_FREE(RQ);
    ABC_BUF_FREE(L);
    ABC_BUF_FREE(L1);
    ABC_BUF_FREE(L2);
    if (pJSON_ERQ) json_decref(pJSON_ERQ);
    if (pJSON_SNRP4) json_decref(pJSON_SNRP4);
    return cc;
}

/**
 * Get the recovery questions for a given account.
 *
 * The questions will be returned in a single allocated string with
 * each questions seperated by a newline.
 *
 * @param szUserName                UserName for the account
 * @param pszQuestions              Pointer into which allocated string should be stored.
 * @param pError                    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginGetRecoveryQuestions(const char *szUserName,
                                        char **pszQuestions,
                                        tABC_Error *pError)
{
    ABC_DebugLog("%s called", __FUNCTION__);

    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(pszQuestions);
    *pszQuestions = NULL;

    // Free up care package cache if set
    ABC_FREE_STR(gCarePackageCache);
    gCarePackageCache = NULL;

    // check that this is a valid user
    if (ABC_LoginCheckValidUser(szUserName, NULL) != ABC_CC_Ok)
    {
        // Try the server
        ABC_CHECK_RET(ABC_LoginFetchRecoveryQuestions(szUserName, pszQuestions, pError));
    }
    else
    {
        // Get RQ for this user
        tABC_U08Buf RQ;
        ABC_CHECK_RET(ABC_LoginGetKey(szUserName, NULL, ABC_LoginKey_RQ, &RQ, pError));
        tABC_U08Buf FinalRQ;
        ABC_BUF_DUP(FinalRQ, RQ);
        ABC_BUF_APPEND_PTR(FinalRQ, "", 1);
        *pszQuestions = (char *)ABC_BUF_PTR(FinalRQ);
    }

exit:

    return cc;
}

/*
 * Gets the account settings filename for a given username
 *
 * @param pszFilename Location to store allocated filename string (caller must free)
 */
static
tABC_CC ABC_LoginGetSettingsFilename(const char *szUserName,
                                       char **pszFilename,
                                       tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    char *szSyncDirName = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pszFilename);
    *pszFilename = NULL;

    ABC_CHECK_RET(ABC_LoginGetSyncDirName(szUserName, &szSyncDirName, pError));

    ABC_ALLOC(*pszFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(*pszFilename, "%s/%s", szSyncDirName, ACCOUNT_SETTINGS_FILENAME);

exit:
    ABC_FREE_STR(szSyncDirName);

    return cc;
}

/**
 * Creates default account settings
 *
 * @param ppSettings    Location to store ptr to allocated settings (caller must free)
 * @param pError        A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginCreateDefaultSettings(tABC_LoginSettings **ppSettings,
                                         tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);
    tABC_LoginSettings *pSettings = NULL;
    int i = 0;

    ABC_CHECK_NULL(ppSettings);

    ABC_ALLOC(pSettings, sizeof(tABC_LoginSettings));

    pSettings->szFirstName = NULL;
    pSettings->szLastName = NULL;
    pSettings->szNickname = NULL;
    pSettings->bNameOnPayments = false;
    pSettings->minutesAutoLogout = 60;
    ABC_STRDUP(pSettings->szLanguage, "en");
    pSettings->currencyNum = CURRENCY_NUM_USD;

    pSettings->exchangeRateSources.numSources = 5;
    ABC_ALLOC(pSettings->exchangeRateSources.aSources,
              pSettings->exchangeRateSources.numSources * sizeof(tABC_ExchangeRateSource *));

    tABC_ExchangeRateSource **aSources = pSettings->exchangeRateSources.aSources;

    // USD defaults to bitstamp
    ABC_ALLOC(aSources[i], sizeof(tABC_ExchangeRateSource));
    aSources[i]->currencyNum = CURRENCY_NUM_USD;
    ABC_STRDUP(aSources[i]->szSource, ABC_BITSTAMP);
    i++;

    // CAD defaults to coinbase
    ABC_ALLOC(aSources[i], sizeof(tABC_ExchangeRateSource));
    aSources[i]->currencyNum = CURRENCY_NUM_CAD;
    ABC_STRDUP(aSources[i]->szSource, ABC_COINBASE);
    i++;

    // EUR defaults to coinbase
    ABC_ALLOC(aSources[i], sizeof(tABC_ExchangeRateSource));
    aSources[i]->currencyNum = CURRENCY_NUM_EUR;
    ABC_STRDUP(aSources[i]->szSource, ABC_COINBASE);
    i++;

    // MXN defaults to coinbase
    ABC_ALLOC(aSources[i], sizeof(tABC_ExchangeRateSource));
    aSources[i]->currencyNum = CURRENCY_NUM_MXN;
    ABC_STRDUP(aSources[i]->szSource, ABC_COINBASE);
    i++;

    // CNY defaults to coinbase
    ABC_ALLOC(aSources[i], sizeof(tABC_ExchangeRateSource));
    aSources[i]->currencyNum = CURRENCY_NUM_CNY;
    ABC_STRDUP(aSources[i]->szSource, ABC_COINBASE);

    pSettings->bitcoinDenomination.denominationType = ABC_DENOMINATION_MBTC;
    pSettings->bitcoinDenomination.satoshi = 100000;

    // assign final settings
    *ppSettings = pSettings;
    pSettings = NULL;

exit:
    ABC_LoginFreeSettings(pSettings);

    return cc;
}

/**
 * Loads the settings for a specific account using the given key
 * If no settings file exists for the given user, defaults are created
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param Key           Key used to decrypt the settings
 * @param ppSettings    Location to store ptr to allocated settings (caller must free)
 * @param pError        A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginLoadSettingsEnc(const char *szUserName,
                                   tABC_U08Buf Key,
                                   tABC_LoginSettings **ppSettings,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_LoginSettings *pSettings = NULL;
    char *szFilename = NULL;
    json_t *pJSON_Root = NULL;
    json_t *pJSON_Value = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL_BUF(Key);
    ABC_CHECK_NULL(ppSettings);

    // get the settings filename
    ABC_CHECK_RET(ABC_LoginGetSettingsFilename(szUserName, &szFilename, pError));

    bool bExists = false;
    ABC_CHECK_RET(ABC_FileIOFileExists(szFilename, &bExists, pError));
    if (true == bExists)
    {
        // load and decrypted the file into a json object
        ABC_CHECK_RET(ABC_CryptoDecryptJSONFileObject(szFilename, Key, &pJSON_Root, pError));
        //ABC_DebugLog("Loaded settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));

        // allocate the new settings object
        ABC_ALLOC(pSettings, sizeof(tABC_LoginSettings));

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
            ABC_ALLOC(pSettings->exchangeRateSources.aSources, pSettings->exchangeRateSources.numSources * sizeof(tABC_ExchangeRateSource *));
        }

        // run through all the sources
        for (int i = 0; i < pSettings->exchangeRateSources.numSources; i++)
        {
            tABC_ExchangeRateSource *pSource = NULL;
            ABC_ALLOC(pSource, sizeof(tABC_ExchangeRateSource));

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
    }
    else
    {
        // create the defaults
        ABC_CHECK_RET(ABC_LoginCreateDefaultSettings(&pSettings, pError));
    }

    // assign final settings
    *ppSettings = pSettings;
    pSettings = NULL;

 //   ABC_DebugLog("Loading settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));

exit:
    ABC_LoginFreeSettings(pSettings);
    ABC_FREE_STR(szFilename);
    if (pJSON_Root) json_decref(pJSON_Root);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Loads the settings for a specific account
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param szPassword    Password for the account associated with the settings
 * @param ppSettings    Location to store ptr to allocated settings (caller must free)
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginLoadSettings(const char *szUserName,
                                const char *szPassword,
                                tABC_LoginSettings **ppSettings,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf LP2 = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(szPassword);
    ABC_CHECK_ASSERT(strlen(szPassword) > 0, ABC_CC_Error, "No password provided");
    ABC_CHECK_NULL(ppSettings);

    // get LP2
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName,szPassword, ABC_LoginKey_LP2, &LP2, pError));

    // load them with the given key
    ABC_CHECK_RET(ABC_LoginLoadSettingsEnc(szUserName, LP2, ppSettings, pError));

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Saves the settings for a specific account using the given key
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param Key           Key used to encrypt the settings
 * @param pSettings     Pointer to settings
 * @param pError        A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginSaveSettingsEnc(const char *szUserName,
                                   tABC_U08Buf Key,
                                   tABC_LoginSettings *pSettings,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    json_t *pJSON_Root = NULL;
    json_t *pJSON_Denom = NULL;
    json_t *pJSON_SourcesArray = NULL;
    json_t *pJSON_Source = NULL;
    char *szFilename = NULL;
    int retVal = 0;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL_BUF(Key);
    ABC_CHECK_NULL(pSettings);

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

    // set language
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_LANGUAGE_FIELD, json_string(pSettings->szLanguage));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set currency num
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_NUM_CURRENCY_FIELD, json_integer(pSettings->currencyNum));
    ABC_CHECK_ASSERT(retVal == 0, ABC_CC_JSONError, "Could not encode JSON value");

    // set advanced features
    retVal = json_object_set_new(pJSON_Root, JSON_ACCT_ADVANCED_FEATURES_FIELD, json_boolean(pSettings->bAdvancedFeatures));
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
    for (int i = 0; i < pSettings->exchangeRateSources.numSources; i++)
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

    // get the settings filename
    ABC_CHECK_RET(ABC_LoginGetSettingsFilename(szUserName, &szFilename, pError));

    // encrypt and save json
//    ABC_DebugLog("Saving settings JSON:\n%s\n", json_dumps(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER));
    ABC_CHECK_RET(ABC_CryptoEncryptJSONFileObject(pJSON_Root, Key, ABC_CryptoType_AES256, szFilename, pError));


exit:
    if (pJSON_Root) json_decref(pJSON_Root);
    if (pJSON_Denom) json_decref(pJSON_Denom);
    if (pJSON_SourcesArray) json_decref(pJSON_SourcesArray);
    if (pJSON_Source) json_decref(pJSON_Source);
    ABC_FREE_STR(szFilename);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Saves the settings for a specific account
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param szPassword    Password for the account associated with the settings
 * @param pSettings     Pointer to settings
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginSaveSettings(const char *szUserName,
                                const char *szPassword,
                                tABC_LoginSettings *pSettings,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf LP2 = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(szPassword);
    ABC_CHECK_ASSERT(strlen(szPassword) > 0, ABC_CC_Error, "No password provided");
    ABC_CHECK_NULL(pSettings);

    // get LP2
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_LP2, &LP2, pError));

    // save them with the given key
    ABC_CHECK_RET(ABC_LoginSaveSettingsEnc(szUserName, LP2, pSettings, pError));

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Free's the given account settings
 */
void ABC_LoginFreeSettings(tABC_LoginSettings *pSettings)
{
    if (pSettings)
    {
        ABC_FREE_STR(pSettings->szFirstName);
        ABC_FREE_STR(pSettings->szLastName);
        ABC_FREE_STR(pSettings->szNickname);
        ABC_FREE_STR(pSettings->szLanguage);
        ABC_FREE_STR(pSettings->szPIN);
        if (pSettings->exchangeRateSources.aSources)
        {
            for (int i = 0; i < pSettings->exchangeRateSources.numSources; i++)
            {
                ABC_FREE_STR(pSettings->exchangeRateSources.aSources[i]->szSource);
                ABC_CLEAR_FREE(pSettings->exchangeRateSources.aSources[i], sizeof(tABC_ExchangeRateSource));
            }
            ABC_CLEAR_FREE(pSettings->exchangeRateSources.aSources, sizeof(tABC_ExchangeRateSource *) * pSettings->exchangeRateSources.numSources);
        }

        ABC_CLEAR_FREE(pSettings, sizeof(tABC_LoginSettings));
    }
}


/**
 * Using the settings pick a repo and create the repo url
 *
 * @param szRepoKey    The repo key
 * @param szRepoPath   Pointer to pointer where the results will be store. Caller must free
 */
tABC_CC ABC_LoginPickRepo(const char *szRepoKey, char **szRepoPath, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    tABC_U08Buf URL = ABC_BUF_NULL;

    ABC_CHECK_NULL(szRepoKey);

    ABC_BUF_DUP_PTR(URL, SYNC_SERVER, strlen(SYNC_SERVER));
    ABC_BUF_APPEND_PTR(URL, szRepoKey, strlen(szRepoKey));
    ABC_BUF_APPEND_PTR(URL, "", 1);

    *szRepoPath = (char *)ABC_BUF_PTR(URL);
    ABC_BUF_CLEAR(URL);
    ABC_BUF_FREE(URL);
exit:
    return cc;
}

/**
 * Sync the account data
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param szPassword    Password for the account associated with the settings
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginSyncData(const char *szUserName,
                            const char *szPassword,
                            int *pDirty,
                            tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tAccountKeys *pKeys = NULL;
    char *szFilename    = NULL;
    char *szAccountDir  = NULL;
    char *szRepoURL     = NULL;

    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));
    ABC_CHECK_ASSERT(NULL != pKeys->szRepoAcctKey, ABC_CC_Error, "Expected to find RepoAcctKey in key cache");

    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Create the repo url and sync it
    ABC_CHECK_RET(ABC_LoginPickRepo(pKeys->szRepoAcctKey, &szRepoURL, pError));

    ABC_DebugLog("URL: %s %s\n", szFilename, szRepoURL);

    // Sync repo
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, szRepoURL, pDirty, pError));
exit:
    ABC_FREE_STR(szRepoURL);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    return cc;
}

/**
 * Locks the mutex
 *
 * ABC_Wallet uses the same mutex as ABC_Login so that there will be no situation in
 * which one thread is in ABC_Wallet locked on a mutex and calling a thread safe ABC_Login call
 * that is locked from another thread calling a thread safe ABC_Wallet call.
 * In other words, since they call each other, they need to share a recursive mutex.
 */
static
tABC_CC ABC_LoginMutexLock(tABC_Error *pError)
{
    return ABC_MutexLock(pError);
}

/**
 * Unlocks the mutex
 *
 */
static
tABC_CC ABC_LoginMutexUnlock(tABC_Error *pError)
{
    return ABC_MutexUnlock(pError);
}