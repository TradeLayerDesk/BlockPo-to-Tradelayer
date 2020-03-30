// Master Protocol transaction code

#include "tradelayer/tx.h"
#include "tradelayer/activation.h"
#include "tradelayer/convert.h"
#include "tradelayer/dex.h"
#include "tradelayer/log.h"
#include "tradelayer/notifications.h"
#include "tradelayer/tradelayer.h"
#include "tradelayer/rules.h"
#include "tradelayer/sp.h"
#include "tradelayer/varint.h"
#include "tradelayer/mdex.h"
#include "tradelayer/uint256_extensions.h"
#include "tradelayer/externfns.h"
#include "tradelayer/parse_string.h"
#include "tradelayer/utilsbitcoin.h"

#include "amount.h"
#include "validation.h"
#include "sync.h"
#include "utiltime.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <utility>
#include <vector>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<inttypes.h>
#include<math.h>
#include "tradelayer_matrices.h"

using boost::algorithm::token_compress_on;
typedef boost::multiprecision::uint128_t ui128;

using namespace mastercore;
typedef boost::rational<boost::multiprecision::checked_int128_t> rational_t;
typedef boost::multiprecision::cpp_dec_float_100 dec_float;
typedef boost::multiprecision::checked_int128_t int128_t;
extern std::map<std::string,uint32_t> peggedIssuers;
extern std::map<uint32_t,std::map<int,oracledata>> oraclePrices;
extern std::map<std::string,vector<withdrawalAccepted>> withdrawal_Map;
extern std::map<uint32_t, std::map<uint32_t, int64_t>> market_priceMap;
extern std::map<std::string,channel> channels_Map;
extern int64_t factorE;
extern int64_t priceIndex;
extern int64_t allPrice;
extern double denMargin;
extern uint64_t marketP[NPTYPES];
extern volatile int id_contract;
extern volatile int64_t factorALLtoLTC;
extern volatile int64_t globalVolumeALL_LTC;
extern volatile int64_t LTCPriceOffer;
extern std::vector<std::string> vestingAddresses;
extern mutex mReward;

using mastercore::StrToInt64;

/** Returns a label for the given transaction type. */
std::string mastercore::strTransactionType(uint16_t txType)
{
  switch (txType)
    {
    case MSC_TYPE_SIMPLE_SEND: return "Simple Send";
    case MSC_TYPE_RESTRICTED_SEND: return "Restricted Send";
    case MSC_TYPE_SEND_ALL: return "Send All";
    case MSC_TYPE_SEND_VESTING: return "Send Vesting Tokens";
    case MSC_TYPE_SAVINGS_MARK: return "Savings";
    case MSC_TYPE_SAVINGS_COMPROMISED: return "Savings COMPROMISED";
    case MSC_TYPE_CREATE_PROPERTY_FIXED: return "Create Property - Fixed";
    case MSC_TYPE_CREATE_PROPERTY_VARIABLE: return "Create Property - Variable";
    case MSC_TYPE_CLOSE_CROWDSALE: return "Close Crowdsale";
    case MSC_TYPE_CREATE_PROPERTY_MANUAL: return "Create Property - Manual";
    case MSC_TYPE_GRANT_PROPERTY_TOKENS: return "Grant Property Tokens";
    case MSC_TYPE_REVOKE_PROPERTY_TOKENS: return "Revoke Property Tokens";
    case MSC_TYPE_CHANGE_ISSUER_ADDRESS: return "Change Issuer Address";
    case TL_MESSAGE_TYPE_ALERT: return "ALERT";
    case TL_MESSAGE_TYPE_DEACTIVATION: return "Feature Deactivation";
    case TL_MESSAGE_TYPE_ACTIVATION: return "Feature Activation";
    case MSC_TYPE_METADEX_TRADE: return "Metadex Order";

    case MSC_TYPE_CONTRACTDEX_TRADE: return "Future Contract";
    case MSC_TYPE_CONTRACTDEX_CANCEL_ECOSYSTEM: return "ContractDex cancel-ecosystem";
    case MSC_TYPE_CREATE_CONTRACT: return "Create Native Contract";
    case MSC_TYPE_PEGGED_CURRENCY: return "Pegged Currency";
    case MSC_TYPE_REDEMPTION_PEGGED: return "Redemption Pegged Currency";
    case MSC_TYPE_SEND_PEGGED_CURRENCY: return "Send Pegged Currency";
    case MSC_TYPE_CONTRACTDEX_CLOSE_POSITION: return "Close Position";
    case MSC_TYPE_CONTRACTDEX_CANCEL_ORDERS_BY_BLOCK: return "Cancel Orders by Block";
    case MSC_TYPE_TRADE_OFFER: return "DEx Sell Offer";
    case MSC_TYPE_DEX_BUY_OFFER: return "DEx Buy Offer";
    case MSC_TYPE_ACCEPT_OFFER_BTC: return "DEx Accept Offer BTC";
    case MSC_TYPE_CHANGE_ORACLE_REF: return "Oracle Change Reference";
    case MSC_TYPE_SET_ORACLE: return "Oracle Set Address";
    case MSC_TYPE_ORACLE_BACKUP: return "Oracle Backup";
    case MSC_TYPE_CLOSE_ORACLE: return "Oracle Close";
    case MSC_TYPE_COMMIT_CHANNEL: return "Channel Commit";
    case MSC_TYPE_WITHDRAWAL_FROM_CHANNEL: return "Channel Withdrawal";
    case MSC_TYPE_INSTANT_TRADE: return "Channel Instant Trade";
    case MSC_TYPE_TRANSFER: return "Channel Transfer";
    case MSC_TYPE_CREATE_CHANNEL: return "Channel Creation";
    case MSC_TYPE_CONTRACT_INSTANT: return "Channel Contract Instant Trade";
    case MSC_TYPE_NEW_ID_REGISTRATION: return "New Id Registration";
    case MSC_TYPE_UPDATE_ID_REGISTRATION: return "Update Id Registration";
    case MSC_TYPE_DEX_PAYMENT: return "DEx payment";
    case MSC_TYPE_ATTESTATION: return "KYC Attestation";
    case MSC_TYPE_CREATE_ORACLE_CONTRACT : return "Create Oracle Contract";
    default: return "* unknown type *";
    }
}

/** Helper to convert class number to string. */
static std::string intToClass(int encodingClass)
{
    switch (encodingClass) {
        case TL_CLASS_D:
            return "D";
    }

    return "-";
}

/** Obtains the next varint from a payload. */
std::vector<uint8_t> CMPTransaction::GetNextVarIntBytes(int &i) {
    std::vector<uint8_t> vecBytes;

    do {
        vecBytes.push_back(pkt[i]);
        if (!IsMSBSet(&pkt[i])) break;
        i++;
    } while (i < pkt_size);

    i++;

    return vecBytes;
}


/** Checks whether a pointer to the payload is past it's last position. */
bool CMPTransaction::isOverrun(const char* p)
{
    ptrdiff_t pos = (char*) p - (char*) &pkt;
    return (pos > pkt_size);
}

// -------------------- PACKET PARSING -----------------------

/** Parses the packet or payload. */
bool CMPTransaction::interpret_Transaction()
{
  if (!interpret_TransactionType()) {
    PrintToLog("Failed to interpret type and version\n");
    return false;
  }

  switch (type)
    {
    case MSC_TYPE_SIMPLE_SEND:
      return interpret_SimpleSend();

    case MSC_TYPE_SEND_ALL:
      return interpret_SendAll();

    case MSC_TYPE_SEND_VESTING:
      return interpret_SendVestingTokens();

    case MSC_TYPE_CREATE_PROPERTY_FIXED:
      return interpret_CreatePropertyFixed();

    case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
      return interpret_CreatePropertyVariable();

    case MSC_TYPE_CLOSE_CROWDSALE:
      return interpret_CloseCrowdsale();

    case MSC_TYPE_CREATE_PROPERTY_MANUAL:
      return interpret_CreatePropertyManaged();

    case MSC_TYPE_GRANT_PROPERTY_TOKENS:
      return interpret_GrantTokens();

    case MSC_TYPE_REVOKE_PROPERTY_TOKENS:
      return interpret_RevokeTokens();

    case MSC_TYPE_CHANGE_ISSUER_ADDRESS:
      return interpret_ChangeIssuer();

    case TL_MESSAGE_TYPE_DEACTIVATION:
      return interpret_Deactivation();

    case TL_MESSAGE_TYPE_ACTIVATION:
      return interpret_Activation();

    case TL_MESSAGE_TYPE_ALERT:
      return interpret_Alert();

    case MSC_TYPE_METADEX_TRADE:
      return interpret_MetaDExTrade();

    case MSC_TYPE_CREATE_CONTRACT:
      return interpret_CreateContractDex();

    case MSC_TYPE_CREATE_ORACLE_CONTRACT:
      return interpret_CreateOracleContract();

    case MSC_TYPE_CONTRACTDEX_TRADE:
      return interpret_ContractDexTrade();

    case MSC_TYPE_CONTRACTDEX_CANCEL_ECOSYSTEM:
      return interpret_ContractDexCancelEcosystem();

    case MSC_TYPE_PEGGED_CURRENCY:
      return interpret_CreatePeggedCurrency();

    case MSC_TYPE_SEND_PEGGED_CURRENCY:
      return interpret_SendPeggedCurrency();

    case MSC_TYPE_REDEMPTION_PEGGED:
      return interpret_RedemptionPegged();

    case MSC_TYPE_CONTRACTDEX_CLOSE_POSITION:
      return interpret_ContractDexClosePosition();

    case MSC_TYPE_CONTRACTDEX_CANCEL_ORDERS_BY_BLOCK:
      return interpret_ContractDex_Cancel_Orders_By_Block();

    case MSC_TYPE_TRADE_OFFER:
      return interpret_TradeOffer();

    case MSC_TYPE_DEX_BUY_OFFER:
      return interpret_DExBuy();

    case MSC_TYPE_ACCEPT_OFFER_BTC:
      return interpret_AcceptOfferBTC();

    case MSC_TYPE_CHANGE_ORACLE_REF:
      return interpret_Change_OracleRef();

    case MSC_TYPE_SET_ORACLE:
      return interpret_Set_Oracle();

    case MSC_TYPE_ORACLE_BACKUP:
      return interpret_OracleBackup();

    case MSC_TYPE_CLOSE_ORACLE:
      return interpret_CloseOracle();

    case MSC_TYPE_COMMIT_CHANNEL:
        return interpret_CommitChannel();

    case MSC_TYPE_WITHDRAWAL_FROM_CHANNEL:
        return interpret_Withdrawal_FromChannel();

    case MSC_TYPE_INSTANT_TRADE:
        return interpret_Instant_Trade();

    case MSC_TYPE_TRANSFER:
        return interpret_Transfer();

    case MSC_TYPE_CREATE_CHANNEL:
        return interpret_Create_Channel();

    case MSC_TYPE_CONTRACT_INSTANT:
        return interpret_Contract_Instant();

    case MSC_TYPE_NEW_ID_REGISTRATION:
        return interpret_New_Id_Registration();

    case MSC_TYPE_UPDATE_ID_REGISTRATION:
        return interpret_Update_Id_Registration();

    case MSC_TYPE_DEX_PAYMENT:
        return interpret_DEx_Payment();

    case MSC_TYPE_ATTESTATION:
            return interpret_Attestation();

    }

  return false;
}

/** Version and type */
bool CMPTransaction::interpret_TransactionType()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t------------------------------\n");
        PrintToLog("\t         version: %d, class %s\n", version, intToClass(encodingClass));
        PrintToLog("\t            type: %d (%s)\n", type, strTransactionType(type));

    }

    return true;
}

/** Tx 1 */
bool CMPTransaction::interpret_SimpleSend()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecPropIdBytes.empty()) {
        property = DecompressInteger(vecPropIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        nValue = DecompressInteger(vecAmountBytes);
        nNewValue = nValue;
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 5 */
bool CMPTransaction::interpret_SendVestingTokens()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

  if (!vecAmountBytes.empty()) {
    nValue = DecompressInteger(vecAmountBytes);
    nNewValue = nValue;
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
    PrintToLog("\t        property: %d (%s)\n", TL_PROPERTY_VESTING, strMPProperty(property));
    PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
  }

  return true;
}

/** Tx 4 */
bool CMPTransaction::interpret_SendAll()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       inside interpret: %d\n");
    }

    return true;
}

/** Tx 50 */
bool CMPTransaction::interpret_CreatePropertyFixed()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    // memcpy(&ecosystem, &pkt[i], 1);
    // i++;

    std::vector<uint8_t> vecPropTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPrevPropIdBytes = GetNextVarIntBytes(i);

    const char* p = i + (char*) &pkt;
    std::vector<std::string> spstr;
    for (int j = 0; j < 3; j++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    int j = 0;
    memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
    memcpy(url, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(url)-1)); j++;
    memcpy(data, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(data)-1)); j++;
    i = i + strlen(name) + strlen(url) + strlen(data) + 3; // data sizes + 3 null terminators

    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecPropTypeBytes.empty()) {
        prop_type = DecompressInteger(vecPropTypeBytes);
    } else return false;

    if (!vecPrevPropIdBytes.empty()) {
        prev_prop_id = DecompressInteger(vecPrevPropIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        nValue = DecompressInteger(vecAmountBytes);
        nNewValue = nValue;
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        // PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
        PrintToLog("\t           value: %s\n", FormatByType(nValue, prop_type));
    }

    return true;
}

/** Tx 51 */
bool CMPTransaction::interpret_CreatePropertyVariable()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    // memcpy(&ecosystem, &pkt[i], 1);
    // i++;

    std::vector<uint8_t> vecPropTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPrevPropIdBytes = GetNextVarIntBytes(i);

    const char* p = i + (char*) &pkt;
    std::vector<std::string> spstr;
    for (int j = 0; j < 3; j++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    int j = 0;
    memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
    memcpy(url, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(url)-1)); j++;
    memcpy(data, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(data)-1)); j++;
    i = i + strlen(name) + strlen(url) + strlen(data) + 3; // data sizes + 3 null terminators

    // std::vector<uint8_t> vecPropertyIdDesiredBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountPerUnitBytes = GetNextVarIntBytes(i);
    // std::vector<uint8_t> vecDeadlineBytes = GetNextVarIntBytes(i);
    // memcpy(&early_bird, &pkt[i], 1);
    // i++;
    // memcpy(&percentage, &pkt[i], 1);
    // i++;

    if (!vecPropTypeBytes.empty()) {
        prop_type = DecompressInteger(vecPropTypeBytes);
    } else return false;

    if (!vecPrevPropIdBytes.empty()) {
        prev_prop_id = DecompressInteger(vecPrevPropIdBytes);
    } else return false;


    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
        PrintToLog("\tproperty desired: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t tokens per unit: %s\n", FormatByType(nValue, prop_type));
    }

    return true;
}

/** Tx 53 */
bool CMPTransaction::interpret_CloseCrowdsale()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropIdBytes = GetNextVarIntBytes(i);

    if (!vecPropIdBytes.empty()) {
        property = DecompressInteger(vecPropIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 54 */
bool CMPTransaction::interpret_CreatePropertyManaged()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    // memcpy(&ecosystem, &pkt[i], 1);
    // i++;

    std::vector<uint8_t> vecPropTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPrevPropIdBytes = GetNextVarIntBytes(i);

    const char* p = i + (char*) &pkt;
    std::vector<std::string> spstr;
    for (int j = 0; j < 3; j++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    int j = 0;
    memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
    memcpy(url, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(url)-1)); j++;
    memcpy(data, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(data)-1)); j++;
    i = i + strlen(name) + strlen(url) + strlen(data) + 3; // data sizes + 3 null terminators

    do
    {
        std::vector<uint8_t> vecKyc = GetNextVarIntBytes(i);
        if (!vecKyc.empty())
        {
            int64_t num = static_cast<int64_t>(DecompressInteger(vecKyc));
            kyc_Ids.push_back(num);
        }

    } while(i < pkt_size);

    if (!vecPropTypeBytes.empty()) {
        prop_type = DecompressInteger(vecPropTypeBytes);
    } else return false;

    if (!vecPrevPropIdBytes.empty()) {
        prev_prop_id = DecompressInteger(vecPrevPropIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
    }

    return true;
}

/** Tx 55 */
bool CMPTransaction::interpret_GrantTokens()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecPropIdBytes.empty()) {
      property = DecompressInteger(vecPropIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
      nValue = DecompressInteger(vecAmountBytes);
      nNewValue = nValue;
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
      PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
      PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 56 */
bool CMPTransaction::interpret_RevokeTokens()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecPropIdBytes.empty()) {
        property = DecompressInteger(vecPropIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        nValue = DecompressInteger(vecAmountBytes);
        nNewValue = nValue;
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 70 */
bool CMPTransaction::interpret_ChangeIssuer()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropIdBytes = GetNextVarIntBytes(i);

    if (!vecPropIdBytes.empty()) {
        property = DecompressInteger(vecPropIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 65533 */
bool CMPTransaction::interpret_Deactivation()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecFeatureIdBytes = GetNextVarIntBytes(i);

    if (!vecFeatureIdBytes.empty()) {
        feature_id = DecompressInteger(vecFeatureIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
    }

    return true;
}

/** Tx 65534 */
bool CMPTransaction::interpret_Activation()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecFeatureIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecActivationBlockBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecMinClientBytes = GetNextVarIntBytes(i);

    if (!vecFeatureIdBytes.empty()) {
        feature_id = DecompressInteger(vecFeatureIdBytes);
    } else return false;

    if (!vecActivationBlockBytes.empty()) {
        activation_block = DecompressInteger(vecActivationBlockBytes);
    } else return false;

    if (!vecMinClientBytes.empty()) {
        min_client_version = DecompressInteger(vecMinClientBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
        PrintToLog("\tactivation block: %d\n", activation_block);
        PrintToLog("\t minimum version: %d\n", min_client_version);
    }

    return true;
}

/** Tx 65535 */
bool CMPTransaction::interpret_Alert()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAlertTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAlertExpiryBytes = GetNextVarIntBytes(i);

    const char* p = i + (char*) &pkt;
    std::string spstr(p);
    memcpy(alert_text, spstr.c_str(), std::min(spstr.length(), sizeof(alert_text)-1));

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    if (!vecAlertTypeBytes.empty()) {
        alert_type = DecompressInteger(vecAlertTypeBytes);
    } else return false;

    if (!vecAlertExpiryBytes.empty()) {
        alert_expiry = DecompressInteger(vecAlertExpiryBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      alert type: %d\n", alert_type);
        PrintToLog("\t    expiry value: %d\n", alert_expiry);
        PrintToLog("\t   alert message: %s\n", alert_text);
    }

    return true;
}

/*Tx 20*/
bool CMPTransaction::interpret_TradeOffer()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropertyIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountDesiredBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTimeLimitBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecMinFeeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecSubActionBytes = GetNextVarIntBytes(i);

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecPropertyIdBytes.empty()) {
        propertyId = DecompressInteger(vecPropertyIdBytes);
    } else return false;

    if (!vecAmountForSaleBytes.empty()) {
        nValue = DecompressInteger(vecAmountForSaleBytes);
        nNewValue = nValue;
    } else return false;

    if (!vecAmountDesiredBytes.empty()) {
        amountDesired = DecompressInteger(vecAmountDesiredBytes);
    } else return false;

    if (!vecTimeLimitBytes.empty()) {
        timeLimit = DecompressInteger(vecTimeLimitBytes);
    } else return false;

    if (!vecMinFeeBytes.empty()) {
        minFee = DecompressInteger(vecMinFeeBytes);
    } else return false;

    if (!vecSubActionBytes.empty()) {
        subAction = DecompressInteger(vecSubActionBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t property: %d\n", propertyId);
        PrintToLog("\t amount : %d\n", nValue);
        PrintToLog("\t amount desired : %d\n", amountDesired);
        PrintToLog("\t block limit : %d\n", timeLimit);
        PrintToLog("\t min fees : %d\n", minFee);
        PrintToLog("\t subaction : %d\n", subAction);
    }

    return true;
}

/*Tx 21*/
bool CMPTransaction::interpret_DExBuy()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropertyIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPriceBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTimeLimitBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecMinFeeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecSubActionBytes = GetNextVarIntBytes(i);

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecPropertyIdBytes.empty()) {
        propertyId = DecompressInteger(vecPropertyIdBytes);
    } else return false;

    if (!vecAmountForSaleBytes.empty()) {
        nValue = DecompressInteger(vecAmountForSaleBytes);
        nNewValue = nValue;
    } else return false;

    if (!vecPriceBytes.empty()) {
        effective_price = DecompressInteger(vecPriceBytes);
    } else return false;

    if (!vecTimeLimitBytes.empty()) {
        timeLimit = DecompressInteger(vecTimeLimitBytes);
    } else return false;

    if (!vecMinFeeBytes.empty()) {
        minFee = DecompressInteger(vecMinFeeBytes);
    } else return false;

    if (!vecSubActionBytes.empty()) {
        subAction = DecompressInteger(vecSubActionBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t property: %d\n", propertyId);
        PrintToLog("\t amount : %d\n", nValue);
        PrintToLog("\t price : %d\n", effective_price);
        PrintToLog("\t block limit : %d\n", timeLimit);
        PrintToLog("\t min fees : %d\n", minFee);
        PrintToLog("\t subaction : %d\n", subAction);
    }

    return true;
}

bool CMPTransaction::interpret_AcceptOfferBTC()
{
  int i = 0;
  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyIdForSaleBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
    type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
    version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecPropertyIdForSaleBytes.empty()) {
    propertyId = DecompressInteger(vecPropertyIdForSaleBytes);
  } else return false;

  if (!vecAmountForSaleBytes.empty()) {
    nValue = DecompressInteger(vecAmountForSaleBytes);
    nNewValue = nValue;
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t property: %d\n", propertyId);
  }

  return true;
}

/** Tx  25*/
bool CMPTransaction::interpret_MetaDExTrade()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropertyIdForSaleBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropertyIdDesiredBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountDesiredBytes = GetNextVarIntBytes(i);

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecPropertyIdForSaleBytes.empty()) {
        property = DecompressInteger(vecPropertyIdForSaleBytes);
    } else return false;

    if (!vecAmountForSaleBytes.empty()) {
        amount_forsale = DecompressInteger(vecAmountForSaleBytes);
        nNewValue = amount_forsale;
    } else return false;

    if (!vecPropertyIdDesiredBytes.empty()) {
        desired_property = DecompressInteger(vecPropertyIdDesiredBytes);
    } else return false;

    if (!vecAmountDesiredBytes.empty()) {
      desired_value = DecompressInteger(vecAmountDesiredBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t property: %d\n", property);
        PrintToLog("\t amount : %d\n", amount_forsale);
        PrintToLog("\t property desired : %d\n", desired_property);
        PrintToLog("\t amount desired : %d\n", desired_value);
    }

    return true;
}

/** Tx  40*/
bool CMPTransaction::interpret_CreateContractDex()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

  // memcpy(&ecosystem, &pkt[i], 1);
  // i++;

  std::vector<uint8_t> vecNum = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecDen = GetNextVarIntBytes(i);

  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 1; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
  i = i + strlen(name) + 1; // data sizes + 1 null terminators

  std::vector<uint8_t> vecBlocksUntilExpiration = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecNotionalSize = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecCollateralCurrency = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecMarginRequirement = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecInverse = GetNextVarIntBytes(i);

  do
  {
      std::vector<uint8_t> vecKyc = GetNextVarIntBytes(i);
      if (!vecKyc.empty())
      {
          int64_t num = static_cast<int64_t>(DecompressInteger(vecKyc));
          kyc_Ids.push_back(num);
      }

  } while(i < pkt_size);

  if (!vecVersionBytes.empty()) {
    version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecTypeBytes.empty()) {
    type = DecompressInteger(vecTypeBytes);
  } else return false;


  if (!vecNum.empty()) {
    numerator = DecompressInteger(vecNum);
  } else return false;

  if (!vecDen.empty()) {
    denominator = DecompressInteger(vecDen);
  } else return false;

  if (!vecBlocksUntilExpiration.empty()) {
    blocks_until_expiration = DecompressInteger(vecBlocksUntilExpiration);
  } else return false;

  if (!vecNotionalSize.empty()) {
    notional_size = DecompressInteger(vecNotionalSize);
  } else return false;

  if (!vecCollateralCurrency.empty()) {
    collateral_currency = DecompressInteger(vecCollateralCurrency);
  } else return false;

  if (!vecMarginRequirement.empty()) {
      margin_requirement = DecompressInteger(vecMarginRequirement);
  } else return false;

  if (!vecInverse.empty()) {
      uint8_t inverse = DecompressInteger(vecInverse);
      if (inverse == 0) inverse_quoted = false;

  } else return false;

  (blocks_until_expiration == 0) ? prop_type = ALL_PROPERTY_TYPE_PERPETUAL_CONTRACTS : prop_type = ALL_PROPERTY_TYPE_NATIVE_CONTRACT;

  // if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  // {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t numerator: %d\n", numerator);
      PrintToLog("\t denominator: %d\n", denominator);
      PrintToLog("\t blocks until expiration : %d\n", blocks_until_expiration);
      PrintToLog("\t notional size : %d\n", notional_size);
      PrintToLog("\t collateral currency: %d\n", collateral_currency);
      PrintToLog("\t margin requirement: %d\n", margin_requirement);
      PrintToLog("\t name: %s\n", name);
      PrintToLog("\t prop_type: %d\n", prop_type);
      PrintToLog("\t inverse quoted: %d\n", inverse_quoted);
  // }

  return true;
}

/**Tx 29 */
bool CMPTransaction::interpret_ContractDexTrade()
{

  int i = 0;
  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

  // PrintToLog("i after two elements: %d\n",i);
  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 1; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(name_traded, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name_traded)-1)); j++;
  i = i + strlen(name_traded) + 1;

  std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecEffectivePriceBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTradingActionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecLeverage = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
    type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
    version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecAmountForSaleBytes.empty()) {
    amount = DecompressInteger(vecAmountForSaleBytes);
  } else return false;

  if (!vecEffectivePriceBytes.empty()) {
    effective_price = DecompressInteger(vecEffectivePriceBytes);
  } else return false;

  if (!vecTradingActionBytes.empty()) {
    trading_action = DecompressInteger(vecTradingActionBytes);
  } else return false;

  if (!vecLeverage.empty()) {
    leverage = DecompressInteger(vecLeverage);
  } else return false;

  // if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  // {
      PrintToLog("\t leverage: %d\n", leverage);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t contractName: %s\n", name_traded);
      PrintToLog("\t amount of contracts : %d\n", amount);
      PrintToLog("\t effective price : %d\n", effective_price);
      PrintToLog("\t trading action : %d\n", trading_action);

  // }

  return true;
}

/** Tx 32 */
bool CMPTransaction::interpret_ContractDexCancelEcosystem()
{
  int i = 0;
  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecEcosystemBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecContractIdBytes = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
    type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
    version = DecompressInteger(vecVersionBytes);
  } else return false;

  // if (!vecEcosystemBytes.empty()) {
  //   ecosystem = DecompressInteger(vecEcosystemBytes);
  // } else return false;

  if (!vecContractIdBytes.empty()) {
    contractId = DecompressInteger(vecContractIdBytes);
  } else return false;

  // if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  // {
     PrintToLog("\t version: %d\n", version);
     PrintToLog("\t messageType: %d\n",type);
     PrintToLog("\t contractId: %d\n", contractId);
  // }

  return true;
}

  /** Tx 33 */
bool CMPTransaction::interpret_ContractDexClosePosition()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecEcosystemBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContractIdBytes = GetNextVarIntBytes(i);

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    // if (!vecEcosystemBytes.empty()) {
    //     ecosystem = DecompressInteger(vecEcosystemBytes);
    // } else return false;

    if (!vecContractIdBytes.empty()) {
        contractId = DecompressInteger(vecContractIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t contractId: %d\n", contractId);
    }

    return true;
}

/** Tx 34 */
bool CMPTransaction::interpret_ContractDex_Cancel_Orders_By_Block()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecBlockBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecIdxBytes = GetNextVarIntBytes(i);

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecTypeBytes.empty()) {
        type = DecompressInteger(vecTypeBytes);
    } else return false;

    if (!vecBlockBytes.empty()) {
        block = DecompressInteger(vecBlockBytes);
    } else return false;

    if (!vecIdxBytes.empty()) {
        tx_idx = DecompressInteger(vecIdxBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t block: %d\n", block);
      PrintToLog("\t tx_idx: %d\n", tx_idx);
    }

    return true;
}

  /** Tx 101 */
bool CMPTransaction::interpret_CreatePeggedCurrency()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    // memcpy(&ecosystem, &pkt[i], 1);
    // i++;

    std::vector<uint8_t> vecPropTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPrevPropIdBytes = GetNextVarIntBytes(i);
    const char* p = i + (char*) &pkt;
    std::vector<std::string> spstr;
    for (int j = 0; j < 1; j++){
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    int j = 0;

    memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
    i = i + strlen(name) + 1; // data sizes + 3 null terminators
    std::vector<uint8_t> vecPropertyIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContractIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecPropTypeBytes.empty()) {
        prop_type = DecompressInteger(vecPropTypeBytes);
    } else return false;

    if (!vecPrevPropIdBytes.empty()) {
        prev_prop_id = DecompressInteger(vecPrevPropIdBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        version = DecompressInteger(vecVersionBytes);
    } else return false;

    if (!vecContractIdBytes.empty()) {
        contractId = DecompressInteger(vecContractIdBytes);
    } else return false;

    if (!vecVersionBytes.empty()) {
        amount = DecompressInteger(vecAmountBytes);
    } else return false;

    if (!vecPropertyIdBytes.empty()) {
        propertyId = DecompressInteger(vecPropertyIdBytes);
    } else return false;

    prop_type = ALL_PROPERTY_TYPE_PEGGEDS;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t property type: %d\n",prop_type);
        PrintToLog("\t prev prop id: %d\n",prev_prop_id);
        PrintToLog("\t contractId: %d\n", contractId);
        PrintToLog("\t propertyId: %d\n", propertyId);
        PrintToLog("\t amount of pegged currency : %d\n", amount);
        PrintToLog("\t name : %d\n", name);
        PrintToLog("\t subcategory: %d\n", subcategory);
    }

    return true;
}

bool CMPTransaction::interpret_SendPeggedCurrency()
{
    int i = 0;

    std::vector<uint8_t> vecMessageVerBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecMessageTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecPropertyIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

    if (!vecMessageVerBytes.empty()) {
        version = DecompressInteger(vecMessageVerBytes);
    } else return false;

    if (!vecMessageTypeBytes.empty()) {
        type = DecompressInteger(vecMessageTypeBytes);
    } else return false;

    if (!vecPropertyIdBytes.empty()) {
        propertyId = DecompressInteger(vecPropertyIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        amount = DecompressInteger(vecAmountBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t propertyId: %d\n", propertyId);
        PrintToLog("\t amount of pegged currency : %d\n", amount);
    }

    return true;
}

bool CMPTransaction::interpret_RedemptionPegged()
{
  int i = 0;

  std::vector<uint8_t> vecMessageVerBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecMessageTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyIdBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecContractIdBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);

  if (!vecMessageVerBytes.empty()) {
    version = DecompressInteger(vecMessageVerBytes);
  } else return false;

  if (!vecMessageTypeBytes.empty()) {
    type = DecompressInteger(vecMessageTypeBytes);
  } else return false;

  if (!vecPropertyIdBytes.empty()) {
    propertyId = DecompressInteger(vecPropertyIdBytes);
  } else return false;

  if (!vecContractIdBytes.empty()) {
    contractId = DecompressInteger(vecContractIdBytes);
  } else return false;

  if (!vecAmountBytes.empty()) {
    amount = DecompressInteger(vecAmountBytes);
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t propertyId: %d\n", propertyId);
      PrintToLog("\t contractId: %d\n", contractId);
      PrintToLog("\t amount of pegged currency : %d\n", amount);
  }

  return true;
}

/** Tx  103*/
bool CMPTransaction::interpret_CreateOracleContract()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

  // memcpy(&ecosystem, &pkt[i], 1);
  // i++;

  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 1; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(name)-1)); j++;
  i = i + strlen(name) + 1; // data sizes + 2 null terminators

  std::vector<uint8_t> vecBlocksUntilExpiration = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecNotionalSize = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecCollateralCurrency = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecMarginRequirement = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecInverse = GetNextVarIntBytes(i);


  if (!vecVersionBytes.empty()) {
    version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecTypeBytes.empty()) {
    type = DecompressInteger(vecTypeBytes);
  } else return false;


  if (!vecBlocksUntilExpiration.empty()) {
    blocks_until_expiration = DecompressInteger(vecBlocksUntilExpiration);
  } else return false;

  if (!vecNotionalSize.empty()) {
    notional_size = DecompressInteger(vecNotionalSize);
  } else return false;

  if (!vecCollateralCurrency.empty()) {
    collateral_currency = DecompressInteger(vecCollateralCurrency);
  } else return false;

  if (!vecMarginRequirement.empty()) {
    margin_requirement = DecompressInteger(vecMarginRequirement);
  } else return false;


  (blocks_until_expiration == 0) ? prop_type = ALL_PROPERTY_TYPE_PERPETUAL_ORACLE : prop_type = ALL_PROPERTY_TYPE_ORACLE_CONTRACT;

  if (!vecInverse.empty()) {
    uint8_t inverse = DecompressInteger(vecInverse);
    if(inverse == 0) inverse_quoted = false;

  } else return false;


  do
  {
      std::vector<uint8_t> vecKyc = GetNextVarIntBytes(i);
      if (!vecKyc.empty())
      {
          int64_t num = static_cast<int64_t>(DecompressInteger(vecKyc));
          kyc_Ids.push_back(num);
      }

  } while(i < pkt_size);


  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      // PrintToLog("\t numerator: %d\n", numerator);
      // PrintToLog("\t denominator: %d\n", denominator);
      PrintToLog("\t blocks until expiration : %d\n", blocks_until_expiration);
      PrintToLog("\t notional size : %d\n", notional_size);
      PrintToLog("\t collateral currency: %d\n", collateral_currency);
      PrintToLog("\t margin requirement: %d\n", margin_requirement);
      PrintToLog("\t name: %s\n", name);
      PrintToLog("\t oracleAddress: %s\n", sender);
      PrintToLog("\t backupAddress: %s\n", receiver);
      PrintToLog("\t prop_type: %d\n", prop_type);
      PrintToLog("\t inverse quoted: %d\n", inverse_quoted);

  }

  return true;
}

/** Tx 104 */
bool CMPTransaction::interpret_Change_OracleRef()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        contractId = DecompressInteger(vecContIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t messageType: %d\n",type);
        PrintToLog("\t contractId: %d\n", contractId);
    }

    return true;
}

/** Tx 105 */
bool CMPTransaction::interpret_Set_Oracle()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecHighBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecLowBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecCloseBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        contractId = DecompressInteger(vecContIdBytes);
    } else return false;

    if (!vecHighBytes.empty()) {
        oracle_high = DecompressInteger(vecHighBytes);
    } else return false;

    if (!vecLowBytes.empty()) {
        oracle_low = DecompressInteger(vecLowBytes);
    } else return false;

    if (!vecLowBytes.empty()) {
        oracle_close = DecompressInteger(vecCloseBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t oracle high price: %d\n",oracle_high);
        PrintToLog("\t oracle low price: %d\n",oracle_low);
        PrintToLog("\t oracle close price: %d\n",oracle_close);
        PrintToLog("\t propertyId: %d\n", propertyId);
    }

    return true;
}

/** Tx 106 */
bool CMPTransaction::interpret_OracleBackup()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        contractId = DecompressInteger(vecContIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t contractId: %d\n", contractId);
    }

    return true;
}

/** Tx 107 */
bool CMPTransaction::interpret_CloseOracle()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        contractId = DecompressInteger(vecContIdBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t contractId: %d\n", contractId);
    }

    return true;
}

/** Tx 108 */
bool CMPTransaction::interpret_CommitChannel()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecVoutBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        propertyId = DecompressInteger(vecContIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        amount_commited = DecompressInteger(vecAmountBytes);
    } else return false;


    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t channelAddress: %s\n", receiver);
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t propertyId: %d\n", propertyId);
        PrintToLog("\t amount commited: %d\n", amount_commited);
    }

    return true;
}

/** Tx 109 */
bool CMPTransaction::interpret_Withdrawal_FromChannel()
{
    int i = 0;

    std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

    std::vector<uint8_t> vecContIdBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecAmountBytes = GetNextVarIntBytes(i);
    std::vector<uint8_t> vecVoutBytes = GetNextVarIntBytes(i);

    if (!vecContIdBytes.empty()) {
        propertyId = DecompressInteger(vecContIdBytes);
    } else return false;

    if (!vecAmountBytes.empty()) {
        amount_to_withdraw = DecompressInteger(vecAmountBytes);
    } else return false;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
    {
        PrintToLog("\t channelAddress: %s\n", receiver);
        PrintToLog("\t version: %d\n", version);
        PrintToLog("\t propertyId: %d\n", propertyId);
        PrintToLog("\t amount to withdrawal: %d\n", amount_to_withdraw);
    }

    return true;
}

/** Tx 110 */
bool CMPTransaction::interpret_Instant_Trade()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyIdForSaleBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmountForSaleBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecBlock = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyIdDesiredBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmountDesiredBytes = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
      type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
      version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecPropertyIdForSaleBytes.empty()) {
      property = DecompressInteger(vecPropertyIdForSaleBytes);
  } else return false;

  if (!vecAmountForSaleBytes.empty()) {
      amount_forsale = DecompressInteger(vecAmountForSaleBytes);
  } else return false;

  if (!vecBlock.empty()) {
      block_forexpiry = DecompressInteger(vecBlock);
  } else return false;

  if (!vecPropertyIdDesiredBytes.empty()) {
      desired_property = DecompressInteger(vecPropertyIdDesiredBytes);
  } else return false;

  if (!vecAmountDesiredBytes.empty()) {
    desired_value = DecompressInteger(vecAmountDesiredBytes);
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t property: %d\n", property);
      PrintToLog("\t amount : %d\n", amount_forsale);
      PrintToLog("\t blockheight_expiry : %d\n", block_forexpiry);
      PrintToLog("\t property desired : %d\n", desired_property);
      PrintToLog("\t amount desired : %d\n", desired_value);
  }

  return true;
}

/** Tx 111 */
bool CMPTransaction::interpret_Update_PNL()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyId = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmount = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecBlock = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecVoutBef = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecVoutPay = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
      type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
      version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecPropertyId.empty()) {
      property = DecompressInteger(vecPropertyId);
  } else return false;

  if (!vecAmount.empty()) {
      pnl_amount = DecompressInteger(vecAmount);
  } else return false;


  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t property: %d\n", property);
      PrintToLog("\t amount : %d\n", amount_forsale);
      PrintToLog("\t property desired : %d\n", desired_property);
      PrintToLog("\t amount desired : %d\n", desired_value);
  }

  return true;
}

/** Tx 112 */
bool CMPTransaction::interpret_Transfer()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPropertyId = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmount = GetNextVarIntBytes(i);

  if (!vecTypeBytes.empty()) {
      type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
      version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecPropertyId.empty()) {
      property = DecompressInteger(vecPropertyId);
  } else return false;

  if (!vecAmount.empty()) {
      amount = DecompressInteger(vecAmount);
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t property: %d\n", property);
      PrintToLog("\t amount : %d\n", amount);
  }

  return true;
}


/** Tx 113 */
bool CMPTransaction::interpret_Create_Channel()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecBlocks = GetNextVarIntBytes(i);

  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 1; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(channel_address, spstr[j].c_str(), spstr[j].length()); j++;
  i = i + strlen(channel_address) + 1; // data sizes + null terminators


  if (!vecTypeBytes.empty()) {
      type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
      version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecBlocks.empty()) {
      block_forexpiry = DecompressInteger(vecBlocks);
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t channelAddress : %d\n",channel_address);
      PrintToLog("\t first address : %d\n", sender);
      PrintToLog("\t second address : %d\n", receiver);
      PrintToLog("\t blocks : %d\n", block_forexpiry);
  }

  return true;
}

/** Tx 114 */
bool CMPTransaction::interpret_Contract_Instant()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecContractId = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecAmount = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecBlock = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecPrice = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTrading = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecLeverage = GetNextVarIntBytes(i);


  if (!vecTypeBytes.empty()) {
      type = DecompressInteger(vecTypeBytes);
  } else return false;

  if (!vecVersionBytes.empty()) {
      version = DecompressInteger(vecVersionBytes);
  } else return false;

  if (!vecContractId.empty()) {
      property = DecompressInteger(vecContractId);
  } else return false;

  if (!vecAmount.empty()) {
      instant_amount = DecompressInteger(vecAmount);
  } else return false;

  if (!vecBlock.empty()) {
      block_forexpiry = DecompressInteger(vecBlock);
  } else return false;

  if (!vecPrice.empty()) {
      price = DecompressInteger(vecPrice);
  } else return false;

  if (!vecTrading.empty()) {
      itrading_action = DecompressInteger(vecTrading);
  } else return false;

  if (!vecLeverage.empty()) {
      ileverage = DecompressInteger(vecLeverage);
  } else return false;

  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("\t version: %d\n", version);
      PrintToLog("\t messageType: %d\n",type);
      PrintToLog("\t property: %d\n", property);
      PrintToLog("\t amount : %d\n", amount_forsale);
      PrintToLog("\t blockfor_expiry : %d\n", block_forexpiry);
      PrintToLog("\t price : %d\n", price);
      PrintToLog("\t trading action : %d\n", itrading_action);
      PrintToLog("\t leverage : %d\n", ileverage);
  }

  return true;
}

/** Tx  115*/
bool CMPTransaction::interpret_New_Id_Registration()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 2; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(website, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(website)-1)); j++;
  memcpy(company_name, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(company_name)-1)); j++;
  i = i + strlen(website) + strlen(company_name) + 2;


  // if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  // {
      PrintToLog("\t address: %s\n", sender);
      PrintToLog("\t website: %s\n", website);
      PrintToLog("\t company name: %s\n", company_name);

  // }

  return true;
}


/** Tx  116*/
bool CMPTransaction::interpret_Update_Id_Registration()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);


  return true;
}

/** Tx  117*/
bool CMPTransaction::interpret_DEx_Payment()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);


  if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  {
      PrintToLog("%s(): inside the function\n",__func__);
      PrintToLog("\t sender: %s\n", sender);
      PrintToLog("\t receiver: %s\n", receiver);
  }

  return true;
}

/** Tx  118*/
bool CMPTransaction::interpret_Attestation()
{
  int i = 0;

  std::vector<uint8_t> vecVersionBytes = GetNextVarIntBytes(i);
  std::vector<uint8_t> vecTypeBytes = GetNextVarIntBytes(i);

  const char* p = i + (char*) &pkt;
  std::vector<std::string> spstr;
  for (int j = 0; j < 1; j++) {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  if (isOverrun(p)) {
    PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
    return false;
  }

  int j = 0;
  memcpy(hash, spstr[j].c_str(), std::min(spstr[j].length(), sizeof(hash)-1)); j++;
  i = i + strlen(hash) + 1;

  // if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly)
  if(true)
  {
      PrintToLog("%s(): hash: %s\n",__func__, hash);
      PrintToLog("\t sender: %s\n", sender);
      PrintToLog("\t receiver: %s\n", receiver);
  }

  return true;
}

// ---------------------- CORE LOGIC -------------------------

/**
 * Interprets the payload and executes the logic.
 *
 * @return  0  if the transaction is fully valid
 *         <0  if the transaction is invalid
 */
int CMPTransaction::interpretPacket()
{
    if (rpcOnly) {
        PrintToLog("%s(): ERROR: attempt to execute logic in RPC mode\n", __func__);
        return (PKT_ERROR -1);
    }

    if (!interpret_Transaction()) {
        return (PKT_ERROR -2);
    }

    LOCK(cs_tally);
    switch (type) {
        case MSC_TYPE_SIMPLE_SEND:
            return logicMath_SimpleSend();

        case MSC_TYPE_SEND_ALL:
            return logicMath_SendAll();

        case MSC_TYPE_SEND_VESTING:
            return logicMath_SendVestingTokens();

        case MSC_TYPE_CREATE_PROPERTY_FIXED:
            return logicMath_CreatePropertyFixed();

        case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
            return logicMath_CreatePropertyVariable();

        case MSC_TYPE_CLOSE_CROWDSALE:
            return logicMath_CloseCrowdsale();

        case MSC_TYPE_CREATE_PROPERTY_MANUAL:
            return logicMath_CreatePropertyManaged();

        case MSC_TYPE_GRANT_PROPERTY_TOKENS:
            return logicMath_GrantTokens();

        case MSC_TYPE_REVOKE_PROPERTY_TOKENS:
            return logicMath_RevokeTokens();

        case MSC_TYPE_CHANGE_ISSUER_ADDRESS:
            return logicMath_ChangeIssuer();

        case TL_MESSAGE_TYPE_DEACTIVATION:
            return logicMath_Deactivation();

        case TL_MESSAGE_TYPE_ACTIVATION:
            return logicMath_Activation();

        case TL_MESSAGE_TYPE_ALERT:
            return logicMath_Alert();

        case MSC_TYPE_CREATE_CONTRACT:
            return logicMath_CreateContractDex();

        case MSC_TYPE_CONTRACTDEX_TRADE:
            return logicMath_ContractDexTrade();

        case MSC_TYPE_CONTRACTDEX_CANCEL_ECOSYSTEM:
            return logicMath_ContractDexCancelEcosystem();

        case MSC_TYPE_PEGGED_CURRENCY:
            return logicMath_CreatePeggedCurrency();

        case MSC_TYPE_SEND_PEGGED_CURRENCY:
            return logicMath_SendPeggedCurrency();

        case MSC_TYPE_REDEMPTION_PEGGED:
            return logicMath_RedemptionPegged();

        case MSC_TYPE_CONTRACTDEX_CLOSE_POSITION:
            return logicMath_ContractDexClosePosition();

        case MSC_TYPE_CONTRACTDEX_CANCEL_ORDERS_BY_BLOCK:
            return logicMath_ContractDex_Cancel_Orders_By_Block();

        case MSC_TYPE_TRADE_OFFER:
            return logicMath_TradeOffer();

        case MSC_TYPE_DEX_BUY_OFFER:
            return logicMath_DExBuy();

        case MSC_TYPE_ACCEPT_OFFER_BTC:
            return logicMath_AcceptOfferBTC();

        case MSC_TYPE_METADEX_TRADE:
            return logicMath_MetaDExTrade();

        case MSC_TYPE_CREATE_ORACLE_CONTRACT:
            return logicMath_CreateOracleContract();

        case MSC_TYPE_CHANGE_ORACLE_REF:
            return logicMath_Change_OracleRef();

        case MSC_TYPE_SET_ORACLE:
            return logicMath_Set_Oracle();

        case MSC_TYPE_ORACLE_BACKUP:
            return logicMath_OracleBackup();

        case MSC_TYPE_CLOSE_ORACLE:
            return logicMath_CloseOracle();

        case MSC_TYPE_COMMIT_CHANNEL:
            return logicMath_CommitChannel();

        case MSC_TYPE_WITHDRAWAL_FROM_CHANNEL:
            return logicMath_Withdrawal_FromChannel();

        case MSC_TYPE_INSTANT_TRADE:
            return logicMath_Instant_Trade();

        case MSC_TYPE_TRANSFER:
            return logicMath_Transfer();

        case MSC_TYPE_CREATE_CHANNEL:
            return logicMath_Create_Channel();

        case MSC_TYPE_CONTRACT_INSTANT:
            return logicMath_Contract_Instant();

        case MSC_TYPE_NEW_ID_REGISTRATION:
            return logicMath_New_Id_Registration();

        case MSC_TYPE_UPDATE_ID_REGISTRATION:
            return logicMath_Update_Id_Registration();

        case MSC_TYPE_DEX_PAYMENT:
            return logicMath_DEx_Payment();

        case MSC_TYPE_ATTESTATION:
            return logicMath_Attestation();


    }

    return (PKT_ERROR -100);
}

/** Passive effect of crowdsale participation. */
int CMPTransaction::logicHelper_CrowdsaleParticipation()
{
    CMPCrowd* pcrowdsale = getCrowd(receiver);

    // No active crowdsale
    if (pcrowdsale == NULL) {
        return (PKT_ERROR_CROWD -1);
    }
    // Active crowdsale, but not for this property
    if (pcrowdsale->getCurrDes() != property) {
        return (PKT_ERROR_CROWD -2);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(pcrowdsale->getPropertyId(), sp));
    // PrintToLog("INVESTMENT SEND to Crowdsale Issuer: %s\n", receiver);

    // Holds the tokens to be credited to the sender and issuer
    std::pair<int64_t, int64_t> tokens;

    // Passed by reference to determine, if max_tokens has been reached
    bool close_crowdsale = false;

    // Units going into the calculateFundraiser function must match the unit of
    // the fundraiser's property_type. By default this means satoshis in and
    // satoshis out. In the condition that the fundraiser is divisible, but
    // indivisible tokens are accepted, it must account for .0 Div != 1 Indiv,
    // but actually 1.0 Div == 100000000 Indiv. The unit must be shifted or the
    // values will be incorrect, which is what is checked below.
    bool inflateAmount = isPropertyDivisible(property) ? false : true;

    // Calculate the amounts to credit for this fundraiser
    calculateFundraiser(inflateAmount, nValue, sp.early_bird, sp.deadline, blockTime,
            sp.num_tokens, sp.percentage, getTotalTokens(pcrowdsale->getPropertyId()),
            tokens, close_crowdsale);

    if (msc_debug_sp) {
        PrintToLog("%s(): granting via crowdsale to user: %s %d (%s)\n",
                __func__, FormatMP(property, tokens.first), property, strMPProperty(property));
        PrintToLog("%s(): granting via crowdsale to issuer: %s %d (%s)\n",
                __func__, FormatMP(property, tokens.second), property, strMPProperty(property));
    }

    // Update the crowdsale object
    pcrowdsale->incTokensUserCreated(tokens.first);
    pcrowdsale->incTokensIssuerCreated(tokens.second);

    // Data to pass to txFundraiserData
    int64_t txdata[] = {(int64_t) nValue, blockTime, tokens.first, tokens.second};
    std::vector<int64_t> txDataVec(txdata, txdata + sizeof(txdata) / sizeof(txdata[0]));

    // Insert data about crowdsale participation
    pcrowdsale->insertDatabase(txid, txDataVec);

    // Credit tokens for this fundraiser
    if (tokens.first > 0) {
        assert(update_tally_map(sender, pcrowdsale->getPropertyId(), tokens.first, BALANCE));
    }
    if (tokens.second > 0) {
        assert(update_tally_map(receiver, pcrowdsale->getPropertyId(), tokens.second, BALANCE));
    }

    // Number of tokens has changed, update fee distribution thresholds
    NotifyTotalTokensChanged(pcrowdsale->getPropertyId());

    // Close crowdsale, if we hit MAX_TOKENS
    if (close_crowdsale) {
        eraseMaxedCrowdsale(receiver, blockTime, block);
    }

    // Indicate, if no tokens were transferred
    if (!tokens.first && !tokens.second) {
        return (PKT_ERROR_CROWD -3);
    }

    return 0;
}

/** Tx 0 */
int CMPTransaction::logicMath_SimpleSend()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SEND -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d", __func__, nValue);
        return (PKT_ERROR_SEND -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SEND -24);
    }

    int kyc_id;

    if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
      PrintToLog("%s(): rejected: kyc ckeck for sender failed\n", __func__);
      return (PKT_ERROR_KYC -10);
    }

    if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
      PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
      return (PKT_ERROR_KYC -20);
    }


    if(!t_tradelistdb->checkAttestationReg(receiver,kyc_id)){
      PrintToLog("%s(): rejected: kyc ckeck for receiver failed\n", __func__);
      return (PKT_ERROR_KYC -10);
    }

    if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
      PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
      return (PKT_ERROR_KYC -20);
    }

    int64_t nBalance = getMPbalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nValue));
        return (PKT_ERROR_SEND -25);
    }

    // ------------------------------------------

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    // Move the tokens
    assert(update_tally_map(sender, property, -nValue, BALANCE));
    assert(update_tally_map(receiver, property, nValue, BALANCE));

    // Is there an active crowdsale running from this recepient?
    logicHelper_CrowdsaleParticipation();

    return 0;
}

/** Tx 5 */
int CMPTransaction::logicMath_SendVestingTokens()
{

  if (!SanityChecks(receiver, block)) {
      PrintToLog("%s(): rejected: sanity checks for send vesting tokens failed\n",
              __func__);
      return (PKT_ERROR_SEND -21);
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              TL_PROPERTY_VESTING,
              block);
      return (PKT_ERROR_SEND -22);
  }

  int64_t nBalance = getMPbalance(sender, TL_PROPERTY_VESTING, BALANCE);
  if (nBalance < (int64_t) nValue) {
      PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
              __func__,
              sender,
              TL_PROPERTY_VESTING,
              FormatMP(TL_PROPERTY_VESTING, nBalance),
              FormatMP(TL_PROPERTY_VESTING, nValue));
      return (PKT_ERROR_SEND -25);
  }

  assert(update_tally_map(sender, TL_PROPERTY_VESTING, -nValue, BALANCE));
  assert(update_tally_map(receiver, TL_PROPERTY_VESTING, nValue, BALANCE));
  assert(update_tally_map(receiver, TL_PROPERTY_ALL, nValue, UNVESTED));

  vestingAddresses.push_back(receiver);

  return 0;
}

/** Tx 4 */
int CMPTransaction::logicMath_SendAll()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SEND_ALL -22);
    }

    // ------------------------------------------

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    CMPTally* ptally = getTally(sender);
    if (ptally == NULL) {
        PrintToLog("%s(): rejected: sender %s has no tokens to send\n", __func__, sender);
        return (PKT_ERROR_SEND_ALL -54);
    }

    uint32_t propertyId = ptally->init();
    int numberOfPropertiesSent = 0;

    while (0 != (propertyId = ptally->next())) {

        int64_t moneyAvailable = ptally->getMoney(propertyId, BALANCE);
        if (moneyAvailable > 0) {
            ++numberOfPropertiesSent;
            assert(update_tally_map(sender, propertyId, -moneyAvailable, BALANCE));
            assert(update_tally_map(receiver, propertyId, moneyAvailable, BALANCE));
            p_txlistdb->recordSendAllSubRecord(txid, numberOfPropertiesSent, propertyId, moneyAvailable);
        }
    }

    if (!numberOfPropertiesSent) {
        PrintToLog("%s(): rejected: sender %s has no tokens to send\n", __func__, sender);
        return (PKT_ERROR_SEND_ALL -55);
    }

    nNewValue = numberOfPropertiesSent;

    return 0;
}

/** Tx 50 */
int CMPTransaction::logicMath_CreatePropertyFixed()
{
  uint256 blockHash;
    {
      LOCK(cs_main);

      CBlockIndex* pindex = chainActive[block];
      if (pindex == NULL) {
	PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
	return (PKT_ERROR_SP -20);
      }
      blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SP -23);
    }

    if (ALL_PROPERTY_TYPE_INDIVISIBLE != prop_type && ALL_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.num_tokens = nValue;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = true;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;

    const uint32_t propertyId = _my_sps->putSP(newSP);
    assert(propertyId > 0);
    assert(update_tally_map(sender, propertyId, nValue, BALANCE));

    NotifyTotalTokensChanged(propertyId);

    return 0;
}

/** Tx 51 */
int CMPTransaction::logicMath_CreatePropertyVariable()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SP -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SP -24);
    }

    if (ALL_PROPERTY_TYPE_INDIVISIBLE != prop_type && ALL_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    if (!deadline || (int64_t) deadline < blockTime) {
        PrintToLog("%s(): rejected: deadline must not be in the past [%d < %d]\n", __func__, deadline, blockTime);
        return (PKT_ERROR_SP -38);
    }

    if (NULL != getCrowd(sender)) {
        PrintToLog("%s(): rejected: sender %s has an active crowdsale\n", __func__, sender);
        return (PKT_ERROR_SP -39);
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.num_tokens = nValue;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = false;
    newSP.property_desired = property;
    newSP.deadline = deadline;
    newSP.early_bird = early_bird;
    newSP.percentage = percentage;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;

    const uint32_t propertyId = _my_sps->putSP(newSP);
    assert(propertyId > 0);
    my_crowds.insert(std::make_pair(sender, CMPCrowd(propertyId, nValue, property, deadline, early_bird, percentage, 0, 0)));

    PrintToLog("CREATED CROWDSALE id: %d value: %d property: %d\n", propertyId, nValue, property);

    return 0;
}

/** Tx 53 */
int CMPTransaction::logicMath_CloseCrowdsale()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SP -24);
    }

    CrowdMap::iterator it = my_crowds.find(sender);
    if (it == my_crowds.end()) {
        PrintToLog("%s(): rejected: sender %s has no active crowdsale\n", __func__, sender);
        return (PKT_ERROR_SP -40);
    }

    const CMPCrowd& crowd = it->second;
    if (property != crowd.getPropertyId()) {
        PrintToLog("%s(): rejected: property identifier mismatch [%d != %d]\n", __func__, property, crowd.getPropertyId());
        return (PKT_ERROR_SP -41);
    }

    // ------------------------------------------

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(property, sp));

    int64_t missedTokens = GetMissedIssuerBonus(sp, crowd);

    sp.historicalData = crowd.getDatabase();
    sp.update_block = blockHash;
    sp.close_early = true;
    sp.timeclosed = blockTime;
    sp.txid_close = txid;
    sp.missedTokens = missedTokens;

    assert(_my_sps->updateSP(property, sp));
    if (missedTokens > 0) {
        assert(update_tally_map(sp.issuer, property, missedTokens, BALANCE));
    }
    my_crowds.erase(it);

    if (msc_debug_sp) PrintToLog("CLOSED CROWDSALE id: %d=%X\n", property, property);

    return 0;
}

/** Tx 54 */
int CMPTransaction::logicMath_CreatePropertyManaged()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (ALL_PROPERTY_TYPE_INDIVISIBLE != prop_type && ALL_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = false;
    newSP.manual = true;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;


    for(std::vector<int64_t>::iterator it = kyc_Ids.begin(); it != kyc_Ids.end(); ++it)
    {
        const int64_t aux = *it;
        newSP.kyc.push_back(aux);
    }


    for(std::vector<int64_t>::iterator itt = newSP.kyc.begin(); itt != newSP.kyc.end(); ++itt)
    {
        const int64_t numb = *itt;
        PrintToLog("%s(): kyc id inside newSP.kyc vector: %d\n",__func__, numb);
    }


    uint32_t propertyId = _my_sps->putSP(newSP);
    assert(propertyId > 0);

    PrintToLog("CREATED MANUAL PROPERTY id: %d admin: %s\n", propertyId, sender);

    CMPSPInfo::Entry sp;
    _my_sps->getSP(propertyId,sp);

    // for(std::vector<int>::iterator it = (sp.kyc).begin(); it != (sp.kyc).end(); ++it)
    // {
    //     PrintToLog("%s(): kyc inside: %d\n",__func__,*(it));
    // }

    return 0;
}

/** Tx 55 */
int CMPTransaction::logicMath_GrantTokens()
{
  uint256 blockHash;
  {
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive[block];
    if (pindex == NULL) {
      PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
      return (PKT_ERROR_SP -20);
    }
    blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
    PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
  	       __func__,
  	       type,
  	       version,
  	       property,
                block);
    return (PKT_ERROR_TOKENS -22);
  }

  if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
    PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
    return (PKT_ERROR_TOKENS -23);
  }

  if (!IsPropertyIdValid(property)) {
    PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
    return (PKT_ERROR_TOKENS -24);
  }

  int kyc_id;

  if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck for sender failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
    return (PKT_ERROR_KYC -20);
  }


  if(!t_tradelistdb->checkAttestationReg(receiver,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck for receiver failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
    return (PKT_ERROR_KYC -20);
  }

  CMPSPInfo::Entry sp;
  assert(_my_sps->getSP(property, sp));

  if (!sp.manual) {
    PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
    return (PKT_ERROR_TOKENS -42);
  }

  if (sender != sp.issuer) {
    PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
    return (PKT_ERROR_TOKENS -43);
  }

  int64_t nTotalTokens = getTotalTokens(property);
  if (nValue > (MAX_INT_8_BYTES - nTotalTokens)) {
    PrintToLog("%s(): rejected: no more than %s tokens can ever exist [%s + %s > %s]\n",
	       __func__,
	       FormatMP(property, MAX_INT_8_BYTES),
	       FormatMP(property, nTotalTokens),
	       FormatMP(property, nValue),
	       FormatMP(property, MAX_INT_8_BYTES));
    return (PKT_ERROR_TOKENS -44);
  }

  // ------------------------------------------

  std::vector<int64_t> dataPt;
  dataPt.push_back(nValue);
  dataPt.push_back(0);
  sp.historicalData.insert(std::make_pair(txid, dataPt));
  sp.update_block = blockHash;
  sp.num_tokens += nValue; // updating created tokens

  // Persist the number of granted tokens
  assert(_my_sps->updateSP(property, sp));

  // Special case: if can't find the receiver -- assume grant to self!
  if (receiver.empty()) {
    receiver = sender;
  }

  // Move the tokens
  assert(update_tally_map(receiver, property, nValue, BALANCE));

  NotifyTotalTokensChanged(property);

  return 0;
}

/** Tx 56 */
int CMPTransaction::logicMath_RevokeTokens()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_TOKENS -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    int64_t nBalance = getMPbalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nValue));
        return (PKT_ERROR_TOKENS -25);
    }

    // ------------------------------------------

    std::vector<int64_t> dataPt;
    dataPt.push_back(0);
    dataPt.push_back(nValue);
    sp.historicalData.insert(std::make_pair(txid, dataPt));
    sp.update_block = blockHash;

    assert(update_tally_map(sender, property, -nValue, BALANCE));
    assert(_my_sps->updateSP(property, sp));

    NotifyTotalTokensChanged(property);

    return 0;
}

/** Tx 70 */
int CMPTransaction::logicMath_ChangeIssuer()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(property, sp));

    if (sender != sp.issuer) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (NULL != getCrowd(sender)) {
        PrintToLog("%s(): rejected: sender %s has an active crowdsale\n", __func__, sender);
        return (PKT_ERROR_TOKENS -39);
    }

    if (receiver.empty()) {
        PrintToLog("%s(): rejected: receiver is empty\n", __func__);
        return (PKT_ERROR_TOKENS -45);
    }

    if (NULL != getCrowd(receiver)) {
        PrintToLog("%s(): rejected: receiver %s has an active crowdsale\n", __func__, receiver);
        return (PKT_ERROR_TOKENS -46);
    }

    // ------------------------------------------

    sp.issuer = receiver;
    sp.update_block = blockHash;

    assert(_my_sps->updateSP(property, sp));

    return 0;
}

/** Tx 65533 */
int CMPTransaction::logicMath_Deactivation()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized
    bool authorized = CheckDeactivationAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized to deactivate features\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    // authorized, request feature deactivation
    bool DeactivationSuccess = DeactivateFeature(feature_id, block);

    if (!DeactivationSuccess) {
        PrintToLog("%s(): DeactivateFeature failed\n", __func__);
        return (PKT_ERROR -54);
    }

    return 0;
}

/** Tx 65534 */
int CMPTransaction::logicMath_Activation()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized - temporarily use alert auths but ## TO BE MOVED TO FOUNDATION P2SH KEY ##
    bool authorized = CheckActivationAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized for feature activations\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    // authorized, request feature activation
    bool activationSuccess = ActivateFeature(feature_id, activation_block, min_client_version, block);

    if (!activationSuccess) {
        PrintToLog("%s(): ActivateFeature failed to activate this feature\n", __func__);
        return (PKT_ERROR -54);
    }

    return 0;
}

/** Tx 65535 */
int CMPTransaction::logicMath_Alert()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized?
    bool authorized = CheckAlertAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized for alerts\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    if (alert_type == 65535) { // set alert type to FFFF to clear previously sent alerts
        DeleteAlerts(sender);
    } else {
        AddAlert(sender, alert_type, alert_expiry, alert_text);
    }

    // we have a new alert, fire a notify event if needed
    // TODO AlertNotify(alert_text);

    return 0;
}

int CMPTransaction::logicMath_MetaDExTrade()
{
  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_METADEX -22);
  }

  if (property == desired_property) {
      PrintToLog("%s(): rejected: property for sale %d and desired property %d must not be equal\n",
              __func__,
              property,
              desired_property);
      return (PKT_ERROR_METADEX -29);
  }

  if (!IsPropertyIdValid(property)) {
      PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
      return (PKT_ERROR_METADEX -31);
  }

  if (!IsPropertyIdValid(desired_property)) {
      PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
      return (PKT_ERROR_METADEX -32);
  }

  int kyc_id;

  if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
    return (PKT_ERROR_KYC -20);
  }

  if(!t_tradelistdb->kycPropertyMatch(desired_property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__,desired_property);
    return (PKT_ERROR_METADEX -34);
  }


  if (nNewValue <= 0 || MAX_INT_8_BYTES < nNewValue) {
      PrintToLog("%s(): rejected: amount for sale out of range or zero: %d\n", __func__, nNewValue);
      return (PKT_ERROR_METADEX -34);
  }

  if (desired_value <= 0 || MAX_INT_8_BYTES < desired_value) {
      PrintToLog("%s(): rejected: desired amount out of range or zero: %d\n", __func__, desired_value);
      return (PKT_ERROR_METADEX -35);
  }

  int64_t nBalance = getMPbalance(sender, property, BALANCE);
  if (nBalance < (int64_t) nNewValue) {
      PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
              __func__,
              sender,
              property,
              FormatMP(property, nBalance),
              FormatMP(property, nNewValue));
      return (PKT_ERROR_METADEX -25);
  }

  // ------------------------------------------

  t_tradelistdb->recordNewTrade(txid, sender, property, desired_property, block, tx_idx);
  int rc = MetaDEx_ADD(sender, property, nNewValue, block, desired_property, desired_value, txid, tx_idx);

  return rc;
}

/** Tx 40 */
int CMPTransaction::logicMath_CreateContractDex()
{
  uint256 blockHash;
  {
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive[block];

      if (pindex == NULL) {
	PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
	return (PKT_ERROR_SP -20);
      }
      blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
          __func__,
          type,
          version,
          propertyId,
          block);
      return (PKT_ERROR_SP -22);
  }

  if ('\0' == name[0]) {
    PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
    return (PKT_ERROR_SP -37);
  }

  // PrintToLog("type of denominator: %d\n",denominator);

  // if (denominator != TL_dUSD && denominator != TL_dEUR && denominator!= TL_dYEN && denominator != TL_ALL && denominator != TL_sLTC && denominator!= TL_LTC) {
  //   PrintToLog("rejected: denominator invalid\n");
  //   return (PKT_ERROR_SP -37);
  // }

  // if (numerator != TL_ALL && numerator != TL_sLTC && numerator != TL_LTC) {
  //   PrintToLog("rejected: denominator invalid\n");
  //   return (PKT_ERROR_SP -37);
  // }

  // -----------------------------------------------

  CMPSPInfo::Entry newSP;
  newSP.txid = txid;
  newSP.issuer = sender;
  newSP.prop_type = prop_type;
  newSP.subcategory.assign(subcategory);
  newSP.name.assign(name);
  newSP.fixed = false;
  newSP.manual = true;
  newSP.creation_block = blockHash;
  newSP.update_block = blockHash;
  newSP.blocks_until_expiration = blocks_until_expiration;
  newSP.notional_size = notional_size;
  newSP.collateral_currency = collateral_currency;
  newSP.margin_requirement = margin_requirement;
  newSP.init_block = block;
  newSP.numerator = numerator;
  newSP.denominator = denominator;
  newSP.attribute_type = attribute_type;
  newSP.expirated = false;
  newSP.inverse_quoted = inverse_quoted;

  for(std::vector<int64_t>::iterator it = kyc_Ids.begin(); it != kyc_Ids.end(); ++it)
  {
      const int64_t aux = *it;
      newSP.kyc.push_back(aux);
  }

  PrintToLog("%s(): init block inside create contract: %d\n", __func__, newSP.init_block);


  const uint32_t propertyId = _my_sps->putSP(newSP);
  assert(propertyId > 0);

  return 0;
}

int CMPTransaction::logicMath_ContractDexTrade()
{
  PrintToLog("%s(): inside functionnn\n",__func__);
  uint256 blockHash;
  {
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive[block];
    if (pindex == NULL)
    {
	      PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
	      return (PKT_ERROR_SP -20);
    }
    blockHash = pindex->GetBlockHash();
  }

  int result;

  struct FutureContractObject *pfuture = getFutureContractObject(name_traded);
  uint32_t contractId = pfuture->fco_propertyId;

  uint32_t expiration = pfuture->fco_blocks_until_expiration;

  (pfuture->fco_prop_type == ALL_PROPERTY_TYPE_NATIVE_CONTRACT) ? result = 5 : result = 6;

  int kyc_id;

  if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(contractId,kyc_id)){
    PrintToLog("%s(): rejected: contract %d can't be traded with this kyc\n", __func__, contractId);
    return (PKT_ERROR_KYC -20);
  }



  PrintToLog("%s(): fco_init_block: %d; fco_blocks_until_expiration: %d; actual block: %d\n",__func__,pfuture->fco_init_block,pfuture->fco_blocks_until_expiration,block);

  if ((block > pfuture->fco_init_block + static_cast<int>(pfuture->fco_blocks_until_expiration) || block < pfuture->fco_init_block) && expiration > 0)
  {
      PrintToLog("%s(): ERROR: Contract expirated \n", __func__);
      return PKT_ERROR_SP -38;
  }


  uint32_t colateralh = pfuture->fco_collateral_currency;
  int64_t marginRe = static_cast<int64_t>(pfuture->fco_margin_requirement);
  int64_t nBalance = getMPbalance(sender, colateralh, BALANCE);

  bool inverse_quoted = pfuture->fco_quoted;

  if(msc_debug_contractdex_tx) PrintToLog("%s():colateralh: %d, marginRe: %d, nBalance: %d\n",__func__, colateralh, marginRe, nBalance);

  // // rational_t conv = notionalChange(pfuture->fco_propertyId);

  int64_t uPrice;

  PrintToLog("inverse quoted: %d\n", inverse_quoted);

  if(inverse_quoted  && market_priceMap[numerator][denominator] > 0)
  {
      uPrice = market_priceMap[numerator][denominator];

  } else if (!inverse_quoted)
      uPrice = COIN;

  PrintToLog("%s(): marginRe: %d,leverage: %d, uPrice: %d\n",__func__, marginRe, leverage, uPrice);

  arith_uint256 amountTR = (ConvertTo256(COIN) * ConvertTo256(amount) * ConvertTo256(marginRe)) / (ConvertTo256(leverage) * ConvertTo256(uPrice));
  int64_t amountToReserve = ConvertTo64(amountTR);

  PrintToLog("%s(): amountToReserve %d\n",__func__,amountToReserve);


  if (nBalance < amountToReserve || nBalance == 0)
    {
      PrintToLog("%s(): rejected: sender %s has insufficient balance for contracts %d [%s < %s] \n",
		 __func__,
		 sender,
		 property,
		 FormatMP(property, nBalance),
		 FormatMP(property, amountToReserve));
      return (PKT_ERROR_SEND -25);
    }
  else
    {
      if (amountToReserve > 0)
	{
	  assert(update_tally_map(sender, colateralh, -amountToReserve, BALANCE));
	  assert(update_tally_map(sender, colateralh,  amountToReserve, CONTRACTDEX_MARGIN));
	}
      // int64_t reserva = getMPbalance(sender, colateralh, CONTRACTDEX_MARGIN);
      // std::string reserved = FormatDivisibleMP(reserva,false);
    }

  /*********************************************/
  /**Logic for Node Reward**/

  const CConsensusParams &params = ConsensusParams();
  int BlockInit = params.MSC_NODE_REWARD;
  int nBlockNow = GetHeight();

  BlockClass NodeRewardObj(BlockInit, nBlockNow);
  NodeRewardObj.SendNodeReward(sender);

  /*********************************************/
  t_tradelistdb->recordNewTrade(txid, sender, contractId, desired_property, block, tx_idx, 0);
  int rc = ContractDex_ADD(sender, contractId, amount, block, txid, tx_idx, effective_price, trading_action,0);

  return rc;
}

/** Tx 32 */
int CMPTransaction::logicMath_ContractDexCancelEcosystem()
{
  if (!IsTransactionTypeAllowed(block, type, version)) {
    PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
	       __func__,
	       type,
	       version,
	       property,
	       block);
    return (PKT_ERROR_CONTRACTDEX -20);
  }

  int rc = ContractDex_CANCEL_EVERYTHING(txid, block, sender, contractId);

  return rc;
}

/** Tx 33 */
int CMPTransaction::logicMath_ContractDexClosePosition()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
            __func__,
            type,
            version,
            property,
            block);
        return (PKT_ERROR_CONTRACTDEX -20);
    }

    CMPSPInfo::Entry sp;
    {
        LOCK(cs_tally);
        if (!_my_sps->getSP(contractId, sp)) {
            PrintToLog(" %s() : Property identifier %d does not exist\n",
                __func__,
                sender,
                contractId);
            return (PKT_ERROR_SEND -24);
        }
    }

    uint32_t collateralCurrency = sp.collateral_currency;
    int rc = ContractDex_CLOSE_POSITION(txid, block, sender, contractId, collateralCurrency);

    return rc;
}

int CMPTransaction::logicMath_ContractDex_Cancel_Orders_By_Block()
{
  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              propertyId,
              block);
     return (PKT_ERROR_METADEX -22);

    }

    ContractDex_CANCEL_FOR_BLOCK(txid, block, tx_idx, sender);

    return 0;
}

/** Tx 100 */
int CMPTransaction::logicMath_CreatePeggedCurrency()
{
    uint256 blockHash;
    uint32_t den;
    uint32_t notSize = 0;
    uint32_t npropertyId = 0;
    int64_t amountNeeded;
    int64_t contracts;

    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }

        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (ALL_PROPERTY_TYPE_INDIVISIBLE != prop_type && ALL_PROPERTY_TYPE_DIVISIBLE != prop_type && ALL_PROPERTY_TYPE_PEGGEDS != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        PrintToLog("rejected: property name must not be empty\n");
        return (PKT_ERROR_SP -37);
    }

    // checking collateral currency
    int64_t nBalance = getMPbalance(sender, propertyId, BALANCE);
    if (nBalance == 0) {
        PrintToLog("%s(): rejected: sender %s has insufficient collateral currency in balance %d \n",
             __func__,
             sender,
             propertyId);
        return (PKT_ERROR_SEND -25);
    }

    CMPSPInfo::Entry sp;
    {
        LOCK(cs_tally);

        if (!_my_sps->getSP(contractId, sp)) {
            PrintToLog(" %s() : Property identifier %d does not exist\n",
                __func__,
                sender,
                contractId);
            return (PKT_ERROR_SEND -24);

        if(!sp.isContract()) {
            PrintToLog(" %s() : Property related is not a contract\n",
                __func__,
                sender,
                contractId);
            return (PKT_ERROR_CONTRACTDEX -21);
        }

        } else if (sp.collateral_currency != propertyId) {
            PrintToLog(" %s() : Future contract has not this collateral currency %d\n",
            __func__,
            sender,
            propertyId);
            return (PKT_ERROR_CONTRACTDEX -22);

        }

        notSize = static_cast<int64_t>(sp.notional_size);
        den = sp.denominator;
    }

    int64_t position = getMPbalance(sender, contractId, NEGATIVE_BALANCE);
    arith_uint256 rAmount = ConvertTo256(amount); // Alls needed
    arith_uint256 Contracts = DivideAndRoundUp(rAmount * ConvertTo256(notSize), ConvertTo256(factorE));
    amountNeeded = ConvertTo64(rAmount);
    contracts = ConvertTo64(Contracts * ConvertTo256(factorE));

    if (nBalance < amountNeeded || position < contracts) {
        PrintToLog("rejected:Sender has not required short position on this contract or balance enough\n");
        return (PKT_ERROR_CONTRACTDEX -23);
    }

    {
        LOCK(cs_tally);
        uint32_t nextSPID = _my_sps->peekNextSPID();
        for (uint32_t propertyId = 1; propertyId < nextSPID; propertyId++) {
            CMPSPInfo::Entry sp;
            if (_my_sps->getSP(propertyId, sp)) {
                if (sp.prop_type == ALL_PROPERTY_TYPE_PEGGEDS && sp.denominator == den){
                    npropertyId = propertyId;
                    break;
                }
            }
        }
    }

    // ------------------------------------------

    if (npropertyId == 0) {   // putting the first one pegged currency of this denominator
        CMPSPInfo::Entry newSP;
        newSP.issuer = sender;
        newSP.txid = txid;
        newSP.prop_type = prop_type;
        newSP.subcategory.assign(subcategory);
        newSP.name.assign(name);
        newSP.fixed = true;
        newSP.manual = true;
        newSP.creation_block = blockHash;
        newSP.update_block = newSP.creation_block;
        newSP.num_tokens = amountNeeded;
        newSP.contracts_needed = contracts;
        newSP.contract_associated = contractId;
        newSP.denominator = den;
        newSP.series = strprintf("Nº 1 - %d",(amountNeeded / factorE));
        npropertyId = _my_sps->putSP(newSP);

    } else {
        CMPSPInfo::Entry newSP;
        _my_sps->getSP(npropertyId, newSP);
        int64_t inf = (newSP.num_tokens) / factorE + 1 ;
        newSP.num_tokens += ConvertTo64(rAmount);
        int64_t sup = (newSP.num_tokens) / factorE ;
        newSP.series = strprintf("Nº %d - %d",inf,sup);
        _my_sps->updateSP(npropertyId, newSP);
    }

    assert(npropertyId > 0);
    CMPSPInfo::Entry SP;
    _my_sps->getSP(npropertyId, SP);
    assert(update_tally_map(sender, npropertyId, amount, BALANCE));
    t_tradelistdb->NotifyPeggedCurrency(txid, sender, npropertyId, amount,SP.series); //TODO: Watch this function!

    // Adding the element to map of pegged currency owners
    peggedIssuers.insert (std::pair<std::string,uint32_t>(sender,npropertyId));

    if (msc_debug_create_pegged)
    {
        PrintToLog("Pegged currency Id: %d\n",npropertyId);
        PrintToLog("CREATED PEGGED PROPERTY id: %d admin: %s\n", npropertyId, sender);
    }


    //putting into reserve contracts and collateral currency
    assert(update_tally_map(sender, contractId, -contracts, NEGATIVE_BALANCE));
    assert(update_tally_map(sender, contractId, contracts, CONTRACTDEX_RESERVE));
    assert(update_tally_map(sender, propertyId, -amountNeeded, BALANCE));
    assert(update_tally_map(sender, propertyId, amountNeeded, CONTRACTDEX_RESERVE));

    return 0;
}

int CMPTransaction::logicMath_SendPeggedCurrency()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
            __func__,
            type,
            version,
            property,
            block);
        return (PKT_ERROR_SEND -22);
    }

    if (!IsPropertyIdValid(propertyId)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SEND -24);
    }

    int64_t nBalance = getMPbalance(sender, propertyId, BALANCE);
    if (nBalance < (int64_t) amount) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
            __func__,
            sender,
            property,
            FormatMP(property, nBalance),
            FormatMP(property, nValue));
        return (PKT_ERROR_SEND -25);
    }

    if (msc_debug_send_pegged)
    {
        PrintToLog("nBalance Pegged Currency Sender : %d \n",nBalance);
        PrintToLog("amount to send of Pegged Currency : %d \n",amount);
    }

    // ------------------------------------------

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    // Move the tokensss

    assert(update_tally_map(sender, propertyId, -amount, BALANCE));
    assert(update_tally_map(receiver, propertyId, amount, BALANCE));

    // Adding the element to map of pegged currency owners
    peggedIssuers.insert (std::pair<std::string,uint32_t>(receiver,propertyId));

    return 0;
}

int CMPTransaction::logicMath_RedemptionPegged()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                propertyId,
                block);
        PrintToLog("rejected: type %d or version %d not permitted for property %d at block %d\n");
        return (PKT_ERROR_SEND -22);
    }

    if (!IsPropertyIdValid(propertyId)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SEND -24);
    }

    int64_t nBalance = getMPbalance(sender, propertyId, BALANCE);
    // int64_t nContracts = getMPbalance(sender, contractId, CONTRACTDEX_RESERVE);
    int64_t negContracts = getMPbalance(sender, contractId, NEGATIVE_BALANCE);
    int64_t posContracts = getMPbalance(sender, contractId, POSITIVE_BALANCE);

    if (nBalance < (int64_t) amount) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of pegged currency %d [%s < %s]\n",
                __func__,
                sender,
                propertyId,
                FormatMP(propertyId, nBalance),
                FormatMP(propertyId, amount));
        return (PKT_ERROR_SEND -25);
    }

    uint32_t collateralId = 0;
    int64_t notSize = 0;

    CMPSPInfo::Entry sp;
    {
        LOCK(cs_tally);
        if (!_my_sps->getSP(contractId, sp)) {
            PrintToLog(" %s() : Property identifier %d does not exist\n",
            __func__,
            sender,
            contractId);
           return (PKT_ERROR_SEND -24);
        }

        collateralId = sp.collateral_currency;
        notSize = static_cast<int64_t>(sp.notional_size);
        sp.num_tokens -= amount;
    }

    arith_uint256 conNeeded = ConvertTo256(amount) / ConvertTo256(notSize);
    int64_t contractsNeeded = ConvertTo64(conNeeded);

    if ((contractsNeeded > 0) && (amount > 0)) {
       // Delete the tokens
       assert(update_tally_map(sender, propertyId, -amount, BALANCE));
       // delete contracts in reserve
       assert(update_tally_map(sender, contractId, -contractsNeeded, CONTRACTDEX_RESERVE));
        // get back the collateral
       assert(update_tally_map(sender, collateralId, -amount, CONTRACTDEX_RESERVE));
       assert(update_tally_map(sender, collateralId, amount, BALANCE));
       if (posContracts > 0 && negContracts == 0)
       {
           int64_t dif = posContracts - contractsNeeded;
           if (dif >= 0)
           {
               assert(update_tally_map(sender, contractId, -contractsNeeded, POSITIVE_BALANCE));
           } else {
               assert(update_tally_map(sender, contractId, -posContracts, POSITIVE_BALANCE));
               assert(update_tally_map(sender, contractId, -dif, NEGATIVE_BALANCE));
           }

       } else if (posContracts == 0 && negContracts >= 0) {
          assert(update_tally_map(sender, contractId, contractsNeeded, NEGATIVE_BALANCE));
       }

    } else {
        PrintToLog("amount redeemed must be equal at least to value of 1 future contract \n");
    }

    return 0;
}

int CMPTransaction::logicMath_TradeOffer()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
            __func__,
            type,
            version,
            propertyId,
            block);
      return (PKT_ERROR_TRADEOFFER -22);
    }

    // if(!t_tradelistdb->egister(sender,4))
    // {
    //     PrintToLog("%s: tx disable from kyc register!\n",__func__);
    //     return (PKT_ERROR_KYC -10);
    // }

    if (MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SEND -23);
    }

  // ------------------------------------------

      int rc = PKT_ERROR_TRADEOFFER;

    // figure out which Action this is based on amount for sale, version & etc.
    switch (version)
    {
        case MP_TX_PKT_V0:
        {
            if (0 != nValue) {

                if (!DEx_offerExists(sender, propertyId)) {
                    PrintToLog("%s():Dex offer doesn't exist\n");
                    rc = DEx_offerCreate(sender, propertyId, nValue, block, amountDesired, minFee, timeLimit, txid, &nNewValue);
                } else {
                    rc = DEx_offerUpdate(sender, propertyId, nValue, block, amountDesired, minFee, timeLimit, txid, &nNewValue);
                }
            } else {
                // what happens if nValue is 0 for V0 ?  ANSWER: check if exists and it does -- cancel, otherwise invalid
                if (DEx_offerExists(sender, propertyId)) {
                    rc = DEx_offerDestroy(sender, propertyId);
                } else {
                    PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, propertyId);
                    rc = (PKT_ERROR_TRADEOFFER -49);
                }
            }

            break;
        }

        case MP_TX_PKT_V1:
        {
            PrintToLog("%s():Case MP_TX_PKT_V1\n");
            if (DEx_offerExists(sender, propertyId)) {
                if (CANCEL != subAction && UPDATE != subAction) {
                    PrintToLog("%s(): rejected: sender %s has an active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -48);
                    break;
                }
            } else {
                // Offer does not exist
                if (NEW != subAction) {
                    PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -49);
                    break;
                }
            }

            switch (subAction) {
                case NEW:
                    PrintToLog("%s():Subaction: NEW\n");
                    rc = DEx_offerCreate(sender, propertyId, nValue, block, amountDesired, minFee, timeLimit, txid, &nNewValue);
                    break;

                case UPDATE:
                    rc = DEx_offerUpdate(sender, propertyId, nValue, block, amountDesired, minFee, timeLimit, txid, &nNewValue);
                    break;

                case CANCEL:
                    rc = DEx_offerDestroy(sender, propertyId);
                    break;

                default:
                    rc = (PKT_ERROR -999);
                    break;
            }
            break;
        }

        default:
            rc = (PKT_ERROR -500); // neither V0 nor V1
            break;
};

  return rc;
}

/*Tx 21*/
int CMPTransaction::logicMath_DExBuy()
{
    if (!IsTransactionTypeAllowed(block, type, version)) {
     PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
             __func__,
             type,
             version,
             propertyId,
             block);
      return (PKT_ERROR_TRADEOFFER -22);
    }

    // if(!t_tradelistdb->checkKYCRegister(sender,4))
    // {
    //     PrintToLog("%s: tx disable from kyc register!\n",__func__);
    //     return (PKT_ERROR_KYC -10);
    // }

    if (MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SEND -23);
    }


  // ------------------------------------------

    int rc = PKT_ERROR_TRADEOFFER;

    // figure out which Action this is based on amount for sale, version & etc.
    switch (version)
      {
      case MP_TX_PKT_V0:
        {
	  if (0 != nValue) {

	    if (!DEx_offerExists(sender, propertyId)) {
	      PrintToLog("%s():Dex offer doesn't exist\n");
	      rc = DEx_BuyOfferCreate(sender, propertyId, nValue, block, effective_price, minFee, timeLimit, txid, &nNewValue);
	    } else {
	      rc = DEx_offerUpdate(sender, propertyId, nValue, block, effective_price, minFee, timeLimit, txid, &nNewValue);
	    }
	  } else {
	    // what happens if nValue is 0 for V0 ?  ANSWER: check if exists and it does -- cancel, otherwise invalid
	    if (DEx_offerExists(sender, propertyId)) {
	      rc = DEx_offerDestroy(sender, propertyId);
	    } else {
	      PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, propertyId);
	      rc = (PKT_ERROR_TRADEOFFER -49);
	    }
	  }

	  break;
        }

        case MP_TX_PKT_V1:
        {
            PrintToLog("%s():Case MP_TX_PKT_V1\n");
            if (DEx_offerExists(sender, propertyId)) {
                if (CANCEL != subAction && UPDATE != subAction) {
                    PrintToLog("%s(): rejected: sender %s has an active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -48);
                    break;
                }
            } else {
                // Offer does not exist
                if (NEW != subAction) {
                    PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -49);
                    break;
                }
            }

            switch (subAction) {
                case NEW:
                    PrintToLog("%s():Subaction: NEW\n");
                    rc = DEx_BuyOfferCreate(sender, propertyId, nValue, block, effective_price, minFee, timeLimit, txid, &nNewValue);
                    break;

              case UPDATE:
                  rc = DEx_offerUpdate(sender, propertyId, nValue, block, effective_price, minFee, timeLimit, txid, &nNewValue);
                  break;

              case CANCEL:
                  rc = DEx_offerDestroy(sender, propertyId);
                  break;

                default:
                    rc = (PKT_ERROR -999);
                    break;
            }
        }
        break;
    }

    return rc;
}

int CMPTransaction::logicMath_AcceptOfferBTC()
{

  if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
    PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
  }

  // if(!t_tradelistdb->checkKYCRegister(sender,4) || !t_tradelistdb->checkKYCRegister(receiver,4))
  // {
  //     PrintToLog("%s: tx disable from kyc register!\n",__func__);
  //     return (PKT_ERROR_KYC -10);
  // }

  // the min fee spec requirement is checked in the following function
  int rc = DEx_acceptCreate(sender, receiver, propertyId, nValue, block, tx_fee_paid, &nNewValue);

  // NOTE: LTC are now added on DEx_payment function!

  /*
  int64_t unitPrice = 0;
  std::string sellerS = "", buyerS = "";

  if (!rc)
    {

      std::string addressFilter = receiver;
      int curBlock = GetHeight();
      if (msc_debug_accept_offerbtc) PrintToLog("\ncurBlock = %d\n", curBlock);

      LOCK(cs_tally);

      for (OfferMap::iterator it = my_offers.begin(); it != my_offers.end(); ++it)
	{
	  const CMPOffer &offer = it->second;
	  const std::string &sellCombo = it->first;
	  std::string seller = sellCombo.substr(0, sellCombo.size() - 2);

	  if (!addressFilter.empty() && seller != addressFilter) continue;

	  std::string txid = offer.getHash().GetHex();
	  uint32_t propertyId = offer.getProperty();
	  int64_t sellOfferAmount = offer.getOfferAmountOriginal();
	  int64_t sellBitcoinDesired = offer.getBTCDesiredOriginal();
	  int64_t amountAvailable = getMPbalance(seller, propertyId, SELLOFFER_RESERVE);
	  uint8_t option = offer.getOption();

	  rational_t unitPriceFloat(sellBitcoinDesired, sellOfferAmount);
	  if(msc_debug_accept_offerbtc) PrintToLog("\nunitPriceFloat = %s\n", xToString(unitPriceFloat));

	  unitPrice = mastercore::StrToInt64(xToString(unitPriceFloat), true);
	  if(msc_debug_accept_offerbtc) PrintToLog("\nunitPriceFloat int64_t= %s\n", FormatDivisibleMP(unitPrice));

	  int64_t bitcoinDesired = calculateDesiredBTC(sellOfferAmount, sellBitcoinDesired, amountAvailable);
	  int64_t sumLtcs = 0;

	  for (AcceptMap::const_iterator ait = my_accepts.begin(); ait != my_accepts.end(); ++ait)
	    {
	      const CMPAccept& accept = ait->second;
	      const std::string& acceptCombo = ait->first;

	      if (accept.getHash() == offer.getHash())
		{
		  std::string buyer = acceptCombo.substr((acceptCombo.find("+") + 1), (acceptCombo.size()-(acceptCombo.find("+") + 1)));
		  int64_t amountOffered = accept.getAcceptAmountRemaining();
		  int64_t amountToPayInBTC = calculateDesiredBTC(accept.getOfferAmountOriginal(), accept.getBTCDesiredOriginal(), amountOffered);

		  if (option == 1)
		    {
		      arith_uint256 ltcsreceived_256t = ConvertTo256(unitPrice)*ConvertTo256(amountOffered);
		      uint64_t ltcsreceived = ConvertTo64(ltcsreceived_256t)/COIN;
		      sumLtcs += ltcsreceived;
		      sellerS = buyer;
		      globalVolumeALL_LTC += ltcsreceived;
		    }
		  else if (option == 2)
		    {
		      buyerS = buyer;
		      globalVolumeALL_LTC += amountToPayInBTC;
		    }
		}

	      if (option == 2)
		{
		  sellerS = seller;
		  globalVolumeALL_LTC += bitcoinDesired;
		}
	      else if (option == 1)
		{
		  buyerS = seller;
		  globalVolumeALL_LTC += sellBitcoinDesired - sumLtcs;
		}
	    }
	}
    }

  const int64_t globalVolumeALL_LTCh = globalVolumeALL_LTC;
  if(msc_debug_accept_offerbtc) PrintToLog("\nglobalVolumeALL_LTC in DEx= %d\n", FormatDivisibleMP(globalVolumeALL_LTCh));
  factorALLtoLTC = unitPrice;
  */

  return rc;
}


/** Tx 103 */
int CMPTransaction::logicMath_CreateOracleContract()
{
  uint256 blockHash;
  {
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive[block];

      if (pindex == NULL)
      {
	        PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
	        return (PKT_ERROR_SP -20);
      }

      blockHash = pindex->GetBlockHash();
  }

  if (sender == receiver)
  {
      PrintToLog("%s(): ERROR: oracle and backup addresses can't be the same!\n", __func__, block);
      return (PKT_ERROR_ORACLE -10);
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
          __func__,
          type,
          version,
          propertyId,
          block);
      return (PKT_ERROR_SP -22);
  }

  if ('\0' == name[0])
  {
      PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
      return (PKT_ERROR_SP -37);
  }

  // -----------------------------------------------

  CMPSPInfo::Entry newSP;
  newSP.txid = txid;
  newSP.issuer = sender;
  newSP.prop_type = prop_type;
  newSP.subcategory.assign(subcategory);
  newSP.name.assign(name);
  newSP.fixed = false;
  newSP.manual = true;
  newSP.creation_block = blockHash;
  newSP.update_block = blockHash;
  newSP.blocks_until_expiration = blocks_until_expiration;
  newSP.notional_size = notional_size;
  newSP.collateral_currency = collateral_currency;
  newSP.margin_requirement = margin_requirement;
  newSP.init_block = block;
  newSP.attribute_type = attribute_type;
  newSP.backup_address = receiver;
  newSP.expirated = false;
  newSP.inverse_quoted = inverse_quoted;
  newSP.oracle_high = 0;
  newSP.oracle_low = 0;
  newSP.oracle_close = 0;

  for(std::vector<int64_t>::iterator it = kyc_Ids.begin(); it != kyc_Ids.end(); ++it)
  {
      const int64_t aux = *it;
      newSP.kyc.push_back(aux);
  }


  const uint32_t propertyId = _my_sps->putSP(newSP);
  assert(propertyId > 0);

  return 0;
}

/** Tx 104 */
int CMPTransaction::logicMath_Change_OracleRef()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(contractId)) {
        PrintToLog("%s(): rejected: oracle contract %d does not exist\n", __func__, property);
        return (PKT_ERROR_ORACLE -11);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(contractId, sp));

    if (sender != sp.issuer) {
        PrintToLog("%s(): rejected: sender %s is not issuer of contract %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_ORACLE -12);
    }

    if (receiver.empty()) {
        PrintToLog("%s(): rejected: receiver is empty\n", __func__);
        return (PKT_ERROR_ORACLE -13);
    }

    // ------------------------------------------

    sp.issuer = receiver;
    sp.update_block = blockHash;

    assert(_my_sps->updateSP(contractId, sp));

    return 0;
}

/** Tx 105 */
int CMPTransaction::logicMath_Set_Oracle()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(contractId)) {
        PrintToLog("%s(): rejected: oracle %d does not exist\n", __func__, property);
        return (PKT_ERROR_ORACLE -11);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(contractId, sp));

    if (sender != sp.issuer) {
        PrintToLog("%s(): rejected: sender %s is not the oracle address of the future contract %d [oracle address=%s]\n", __func__, sender, contractId, sp.issuer);
        return (PKT_ERROR_ORACLE -12);
    }


    // ------------------------------------------
    oracledata Ol;

    Ol.high = oracle_high;
    Ol.low = oracle_low;
    Ol.close = oracle_close;

    oraclePrices[contractId][block] = Ol;


    // PrintToLog("%s():Ol element:,high:%d, low:%d, close:%d\n",__func__, Ol.high, Ol.low, Ol.close);


    // saving on db
    sp.oracle_high = oracle_high;
    sp.oracle_low = oracle_low;
    sp.oracle_close = oracle_close;


   if(oraclePrices.empty())
       PrintToLog("%s(): element was not inserted !\n",__func__);
   else
       PrintToLog("%s(): element was INSERTED \n",__func__);
    //
    // std::map<uint32_t,std::map<int,oracledata>>::iterator it = oraclePrices.find(contractId);
    //
    //
    // std::map<int,oracledata> m = it->second;
    //
    // std::map<int,oracledata>::iterator itt = m.find(block);
    //
    // oracledata Or = itt->second;
    //
    // PrintToLog("%s(): oracle data for contract: block: %d,high:%d, low:%d, close:%d\n",block, Or.high, Or.low, Or.close);



    assert(_my_sps->updateSP(contractId, sp));

    // if (msc_debug_set_oracle) PrintToLog("oracle data for contract: block: %d,high:%d, low:%d, close:%d\n",block, oracle_high, oracle_low, oracle_close);

    return 0;
}

/** Tx 106 */
int CMPTransaction::logicMath_OracleBackup()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(contractId)) {
        PrintToLog("%s(): rejected: oracle %d does not exist\n", __func__, property);
        return (PKT_ERROR_ORACLE -11);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(contractId, sp));

    if (sender != sp.backup_address) {
        PrintToLog("%s(): rejected: sender %s is not the backup address of the Oracle Future Contract\n", __func__,sender);
        return (PKT_ERROR_ORACLE -14);
    }

    // ------------------------------------------

    sp.issuer = sender;
    sp.update_block = blockHash;

    assert(_my_sps->updateSP(contractId, sp));

    return 0;
}

/** Tx 107 */
int CMPTransaction::logicMath_CloseOracle()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(contractId)) {
        PrintToLog("%s(): rejected: oracle %d does not exist\n", __func__, property);
        return (PKT_ERROR_ORACLE -11);
    }

    CMPSPInfo::Entry sp;
    assert(_my_sps->getSP(contractId, sp));

    if (sender != sp.backup_address) {
        PrintToLog("%s(): rejected: sender %s is not the backup address of the Oracle Future Contract\n", __func__,sender);
        return (PKT_ERROR_ORACLE -14);
    }

    // ------------------------------------------

    sp.blocks_until_expiration = 0;

    assert(_my_sps->updateSP(contractId, sp));

    PrintToLog("%s(): Oracle Contract (id:%d) Closed\n", __func__,contractId);

    return 0;
}

/** Tx 108 */
int CMPTransaction::logicMath_CommitChannel()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!t_tradelistdb->checkChannelAddress(receiver)) {
        PrintToLog("%s(): rejected: address %s doesn't belong to multisig channel\n", __func__, receiver);
        return (PKT_ERROR_CHANNELS -10);
    }

    if (!IsPropertyIdValid(propertyId)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }


    // ------------------------------------------

    // logic for the commit Here

    if(msc_debug_commit_channel) PrintToLog("%s():sender: %s, channelAddress: %s\n",__func__, sender, receiver);

    //putting money into channel reserve
    assert(update_tally_map(sender, propertyId, -amount_commited, BALANCE));
    assert(update_tally_map(receiver, propertyId, amount_commited, CHANNEL_RESERVE));

    t_tradelistdb->recordNewCommit(txid, receiver, sender, propertyId, amount_commited, block, tx_idx);

    int64_t amountCheck = getMPbalance(receiver, propertyId,CHANNEL_RESERVE);

    if(msc_debug_commit_channel) PrintToLog("amount inside channel multisig: %s\n",amountCheck);

    return 0;
}

/** Tx 109 */
int CMPTransaction::logicMath_Withdrawal_FromChannel()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(propertyId)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    if (!t_tradelistdb->checkChannelAddress(receiver)) {
        PrintToLog("%s(): rejected: address %s doesn't belong to multisig channel\n", __func__, receiver);
        return (PKT_ERROR_CHANNELS -10);
    }


    // ------------------------------------------

    //checking balance of channelAddress
    uint64_t totalAmount = static_cast<uint64_t>(getMPbalance(receiver, propertyId, CHANNEL_RESERVE));

    if (msc_debug_withdrawal_from_channel) PrintToLog("%s(): amount_to_withdraw : %d, totalAmount in channel: %d\n", __func__, amount_to_withdraw, totalAmount);

    if (amount_to_withdraw > totalAmount)
    {
        PrintToLog("%s(): amount to withdrawal is bigger than totalAmount on channel\n", __func__);
        return (PKT_ERROR_TOKENS -25);
    }

    uint64_t amount_remaining = t_tradelistdb->getRemaining(receiver, sender, propertyId);

    if (msc_debug_withdrawal_from_channel) PrintToLog("all the amount remaining for the receiver address : %s\n",amount_remaining);

    if (amount_to_withdraw > amount_remaining)
    {
        PrintToLog("%s(): amount to withdrawal is bigger than amount remaining in channel for the address %s\n", __func__, sender);
        return (PKT_ERROR_TOKENS -26);
    }

    withdrawalAccepted wthd;

    wthd.address = sender;
    wthd.deadline_block = block + 7;
    wthd.propertyId = propertyId;
    wthd.amount = amount_to_withdraw;

    if (msc_debug_withdrawal_from_channel) PrintToLog("checking wthd element : address: %s, deadline: %d, propertyId: %d, amount: %d \n", wthd.address, wthd.deadline_block, wthd.propertyId, wthd.amount);

    withdrawal_Map[receiver].push_back(wthd);

    t_tradelistdb->recordNewWithdrawal(txid, receiver, sender, propertyId, amount_to_withdraw, block, tx_idx);

    return 0;
}


/** Tx 110 */
int CMPTransaction::logicMath_Instant_Trade()
{
  int rc = 0;

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_METADEX -22);
  }

  if (property == desired_property) {
      PrintToLog("%s(): rejected: property for sale %d and desired property %d must not be equal\n",
              __func__,
              property,
              desired_property);
      return (PKT_ERROR_CHANNELS -11);
  }

  if (!IsPropertyIdValid(property)) {
      PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
      return (PKT_ERROR_CHANNELS -13);
  }

  if (!IsPropertyIdValid(desired_property)) {
      PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
      return (PKT_ERROR_CHANNELS -14);
  }

  int kyc_id;

  if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, property);
    return (PKT_ERROR_KYC -20);
  }

  if(!t_tradelistdb->kycPropertyMatch(desired_property,kyc_id)){
    PrintToLog("%s(): rejected: property %d can't be traded with this kyc\n", __func__, desired_property);
    return (PKT_ERROR_KYC -20);
  }

  channel chnAddrs = t_tradelistdb->getChannelAddresses(sender);

  if (sender.empty() && chnAddrs.first.empty() && chnAddrs.second.empty()) {
      PrintToLog("%s(): rejected: some address doesn't belong to multisig channel \n", __func__);
      return (PKT_ERROR_CHANNELS -15);
  }

  if (chnAddrs.expiry_height < block) {
      PrintToLog("%s(): rejected: out of channel deadline: actual block: %d, deadline: %d\n", __func__, block, chnAddrs.expiry_height);
      return (PKT_ERROR_CHANNELS -16);
  }

  int64_t nBalance = getMPbalance(sender, property, CHANNEL_RESERVE);
  if (property > 0 && nBalance < (int64_t) amount_forsale) {
      PrintToLog("%s(): rejected: channel address %s has insufficient balance of property %d [%s < %s]\n",
              __func__,
              sender,
              property,
              FormatMP(property, nBalance),
              FormatMP(property, amount_forsale));
      return (PKT_ERROR_CHANNELS -17);
  }

  nBalance = getMPbalance(sender, desired_property, CHANNEL_RESERVE);
  if (desired_property > 0 && nBalance < (int64_t) desired_value) {
      PrintToLog("%s(): rejected: channel address %s has insufficient balance of property %d [%s < %s]\n",
              __func__,
              sender,
              desired_property,
              FormatMP(desired_property, nBalance),
              FormatMP(desired_property, desired_value));
      return (PKT_ERROR_CHANNELS -17);
  }

  // ------------------------------------------

  // if property = 0 ; we are exchanging litecoins
  // if (false)
  if (property > LTC && desired_property > 0)
  {
      assert(update_tally_map(chnAddrs.second, property, amount_forsale, BALANCE));
      assert(update_tally_map(sender, property, -amount_forsale, CHANNEL_RESERVE));
      assert(update_tally_map(chnAddrs.first, desired_property, desired_value, BALANCE));
      assert(update_tally_map(sender, desired_property, -desired_value, CHANNEL_RESERVE));

      t_tradelistdb->recordNewInstantTrade(txid, sender, chnAddrs.first, property, amount_forsale, desired_property, desired_value, block, tx_idx);

      // NOTE: require discount for address and tokens (to consider commits and withdrawals too)

      // updating last exchange block
      std::map<std::string,channel>::iterator it = channels_Map.find(sender);
      channel& chn = it->second;

      int difference = block - chn.last_exchange_block;

      if (msc_debug_instant_trade) PrintToLog("expiry height after update: %d\n",chn.expiry_height);

      // updating expiry_height
      if (difference < dayblocks) chn.expiry_height += difference;


  } else {

      assert(update_tally_map(chnAddrs.first, desired_property, desired_value, BALANCE));
      assert(update_tally_map(sender, desired_property, -desired_value, CHANNEL_RESERVE));
      rc = 1;
      if(msc_debug_instant_trade) PrintToLog("Trading litecoins vs tokens\n");

  }

  return rc;
}

/** Tx 111 */
int CMPTransaction::logicMath_Update_PNL()
{
  uint256 blockHash;
  {
      LOCK(cs_main);

      CBlockIndex* pindex = chainActive[block];
      if (pindex == NULL) {
          PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
          return (PKT_ERROR_TOKENS -20);
      }
      blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_TOKENS -22);
  }

  if (!IsPropertyIdValid(propertyId)) {
      PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
      return (PKT_ERROR_CHANNELS -13);
  }


  // ------------------------------------------


  //logic for PNLS
  assert(update_tally_map(sender, propertyId, -pnl_amount, CHANNEL_RESERVE));
  assert(update_tally_map(receiver, propertyId, pnl_amount, BALANCE));


  return 0;

}

/** Tx 112 */
int CMPTransaction::logicMath_Transfer()
{
  uint256 blockHash;
  {
      LOCK(cs_main);

      CBlockIndex* pindex = chainActive[block];
      if (pindex == NULL) {
          PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
          return (PKT_ERROR_TOKENS -20);
      }
      blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_TOKENS -22);
  }

  if (!IsPropertyIdValid(propertyId)) {
      PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
      return (PKT_ERROR_CHANNELS -13);
  }


  // ------------------------------------------


  // TRANSFER logic here

  assert(update_tally_map(sender, propertyId, -amount, CHANNEL_RESERVE));
  assert(update_tally_map(receiver, propertyId, amount, CHANNEL_RESERVE));

  // recordNewTransfer
  t_tradelistdb->recordNewTransfer(txid, sender,receiver, propertyId, amount, block, tx_idx);

  return 0;

}

/** Tx 113*/
int CMPTransaction::logicMath_Create_Channel()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }


    // ------------------------------------------

    int expiry_height = block + block_forexpiry;

    channel chn;

    chn.multisig = channel_address;
    chn.first = sender;
    chn.second = receiver;
    chn.expiry_height = expiry_height;

    if(msc_create_channel) PrintToLog("checking channel elements : channel address: %s, first address: %d, second address: %d, expiry_height: %d \n", chn.multisig, chn.first, chn.second, chn.expiry_height);

    channels_Map[channel_address] = chn;

    t_tradelistdb->recordNewChannel(channel_address,sender,receiver, expiry_height, tx_idx);

    return 0;
}

/** Tx 114 */
int CMPTransaction::logicMath_Contract_Instant()
{
  int rc = 0;


  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_METADEX -22);
  }


  if (!IsPropertyIdValid(property)) {
      PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
      return (PKT_ERROR_CHANNELS -13);
  }

  if (!IsPropertyIdValid(desired_property)) {
      PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
      return (PKT_ERROR_CHANNELS -14);
  }

  channel chnAddrs = t_tradelistdb->getChannelAddresses(sender);

  if (sender.empty() || chnAddrs.first.empty() || chnAddrs.second.empty()) {
      PrintToLog("%s(): rejected: some address doesn't belong to multisig channel\n", __func__);
      return (PKT_ERROR_CHANNELS -15);
  }

  if (chnAddrs.expiry_height < block) {
      PrintToLog("%s(): rejected: out of channel deadline: actual block: %d, deadline: %d\n", __func__, block, chnAddrs.expiry_height);
      return (PKT_ERROR_CHANNELS -16);
  }

  CMPSPInfo::Entry sp;
  if (!_my_sps->getSP(property, sp))
      return (PKT_ERROR_CHANNELS -13);


  if (block > sp.init_block + static_cast<int>(sp.blocks_until_expiration) || block < sp.init_block)
  {
      int initblock = sp.init_block ;
      int deadline = initblock + static_cast<int>(sp.blocks_until_expiration);
      PrintToLog("\nTrade out of deadline!!: actual block: %d, deadline: %d\n",initblock,deadline);
      return (PKT_ERROR_CHANNELS -16);
  }

  int kyc_id;

  if(!t_tradelistdb->checkAttestationReg(sender,kyc_id)){
    PrintToLog("%s(): rejected: kyc ckeck failed\n", __func__);
    return (PKT_ERROR_KYC -10);
  }

  if(!t_tradelistdb->kycPropertyMatch(property,kyc_id)){
    PrintToLog("%s(): rejected: contract %d can't be traded with this kyc\n", __func__, property);
    return (PKT_ERROR_KYC -20);
  }


  uint32_t colateralh = sp.collateral_currency;
  int64_t marginRe = static_cast<int64_t>(sp.margin_requirement);
  int64_t nBalance = getMPbalance(sender, colateralh, CHANNEL_RESERVE);

  arith_uint256 amountTR = (ConvertTo256(instant_amount)*ConvertTo256(marginRe))/ConvertTo256(ileverage);
  int64_t amountToReserve = ConvertTo64(amountTR);

  if(msc_debug_contract_instant_trade) PrintToLog("%s: AmountToReserve: %d, channel Balance: %d\n", __func__, amountToReserve,nBalance);

  if(msc_debug_contract_instant_trade) PrintToLog("%s: sender: %s, channel Address: %s\n", __func__, sender, chnAddrs.multisig);


  //fees
  if(!mastercore::ContInst_Fees(chnAddrs.first, chnAddrs.second, chnAddrs.multisig, amountToReserve, sp.prop_type, sp.collateral_currency))
  {
      PrintToLog("\n %s: no enogh money to pay fees\n", __func__);
      return (PKT_ERROR_CHANNELS -18);
  }


  if (amountToReserve > 0)
  {
      assert(update_tally_map(sender, colateralh, -amountToReserve, CHANNEL_RESERVE));
      assert(update_tally_map(chnAddrs.first, colateralh, ConvertTo64(amountTR), CONTRACTDEX_MARGIN));
      assert(update_tally_map(chnAddrs.second, colateralh, ConvertTo64(amountTR), CONTRACTDEX_MARGIN));
  }


   /*********************************************/
   /**Logic for Node Reward**/

   // const CConsensusParams &params = ConsensusParams();
   // int BlockInit = params.MSC_NODE_REWARD;
   // int nBlockNow = GetHeight();
   //
   // BlockClass NodeRewardObj(BlockInit, nBlockNow);
   // NodeRewardObj.SendNodeReward(sender);

   /********************************************************/

   // updating last exchange block
   std::map<std::string,channel>::iterator it = channels_Map.find(sender);
   channel& chn = it->second;

   int difference = block - chn.last_exchange_block;

   if(msc_debug_contract_instant_trade) PrintToLog("%s: expiry height after update: %d\n",__func__, chn.expiry_height);

   if (difference < dayblocks)
   {
       // updating expiry_height
       chn.expiry_height += difference;

   }

   mastercore::Instant_x_Trade(txid, itrading_action, chnAddrs.multisig, chnAddrs.first, chnAddrs.second, property, instant_amount, price, block, tx_idx);

   // t_tradelistdb->recordNewInstContTrade(txid, receiver, sender, propertyId, amount_commited, block, tx_idx);
   // NOTE: add discount from channel of fees + amountToReserve

   if (msc_debug_contract_instant_trade)PrintToLog("%s: End of Logic Instant Contract Trade\n\n",__func__);


   return rc;
}

/** Tx 115 */
int CMPTransaction::logicMath_New_Id_Registration()
{
  uint256 blockHash;
  {
      LOCK(cs_main);

      CBlockIndex* pindex = chainActive[block];
      if (pindex == NULL) {
          PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
          return (PKT_ERROR_TOKENS -20);
      }
      blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_TOKENS -22);
  }

  int kyc_id;

  if(t_tradelistdb->checkKYCRegister(sender,kyc_id)){
    PrintToLog("%s(): rejected: address is on kyc register yet\n", __func__);
    return (PKT_ERROR_KYC -10);
  }


  // ---------------------------------------
  if (msc_debug_new_id_registration) PrintToLog("%s(): channelAddres in register: %s \n",__func__,receiver);

  t_tradelistdb->recordNewIdRegister(txid, sender, company_name, website, block, tx_idx);

  // std::string dummy = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";
  // t_tradelistdb->updateIdRegister(txid,sender, dummy,block, tx_idx);
  return 0;
}

/** Tx 116 */
int CMPTransaction::logicMath_Update_Id_Registration()
{
  uint256 blockHash;
  {
      LOCK(cs_main);

      CBlockIndex* pindex = chainActive[block];
      if (pindex == NULL) {
          PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
          return (PKT_ERROR_TOKENS -20);
      }
      blockHash = pindex->GetBlockHash();
  }

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_TOKENS -22);
  }

  // ---------------------------------------

  t_tradelistdb->updateIdRegister(txid,sender, receiver,block, tx_idx);

  return 0;
}

/** Tx 117 */
int CMPTransaction::logicMath_DEx_Payment()
{
  int rc = 0;

  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_METADEX -22);
  }

  // PrintToLog("%s(): inside the function\n",__func__);

  rc = 2;

  return rc;
}

/** Tx 118 */
int CMPTransaction::logicMath_Attestation()
{
  if (!IsTransactionTypeAllowed(block, type, version)) {
      PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
              __func__,
              type,
              version,
              property,
              block);
      return (PKT_ERROR_METADEX -22);
  }

  int kyc_id;

  if(!t_tradelistdb->checkKYCRegister(sender,kyc_id))
  {
      kyc_id = KYC_0;

      if (sender != receiver)
      {
          PrintToLog("%s(): rejected: sender (%s) can't assign attestation to other address\n",
              __func__,
              sender);
          return (PKT_ERROR_METADEX -22);
      }

  }


  PrintToLog("%s(): kyc_id: %d\n",__func__,kyc_id);

  t_tradelistdb->recordNewAttestation(txid, receiver, block, tx_idx, kyc_id);

  return 0;

}

struct FutureContractObject *getFutureContractObject(std::string identifier)
{
  struct FutureContractObject *pt_fco = new FutureContractObject;

  LOCK(cs_tally);
  uint32_t nextSPID = _my_sps->peekNextSPID();
  for (uint32_t propertyId = 1; propertyId < nextSPID; propertyId++)
    {
      CMPSPInfo::Entry sp;
      if (_my_sps->getSP(propertyId, sp))
	{
	  if ( sp.isContract() && sp.name == identifier )
	    {
        pt_fco->fco_denominator = sp.numerator;
	      pt_fco->fco_denominator = sp.denominator;
	      pt_fco->fco_blocks_until_expiration = sp.blocks_until_expiration;
	      pt_fco->fco_notional_size = sp.notional_size;
	      pt_fco->fco_collateral_currency = sp.collateral_currency;
	      pt_fco->fco_margin_requirement = sp.margin_requirement;
	      pt_fco->fco_name = sp.name;
	      pt_fco->fco_subcategory = sp.subcategory;
	      pt_fco->fco_issuer = sp.issuer;
	      pt_fco->fco_init_block = sp.init_block;
        pt_fco->fco_backup_address = sp.backup_address;
	      pt_fco->fco_propertyId = propertyId;
        pt_fco->fco_prop_type = sp.prop_type;
        pt_fco->fco_expirated = sp.expirated;
        pt_fco->fco_quoted = sp.inverse_quoted;
	    }
	  else if ( sp.isPegged() && sp.name == identifier )
	    {
	      pt_fco->fco_denominator = sp.denominator;
	      pt_fco->fco_blocks_until_expiration = sp.blocks_until_expiration;
	      pt_fco->fco_notional_size = sp.notional_size;
	      pt_fco->fco_collateral_currency = sp.collateral_currency;
	      pt_fco->fco_margin_requirement = sp.margin_requirement;
	      pt_fco->fco_name = sp.name;
	      pt_fco->fco_subcategory = sp.subcategory;
	      pt_fco->fco_issuer = sp.issuer;
	      pt_fco->fco_init_block = sp.init_block;
	      pt_fco->fco_propertyId = propertyId;
	    }
	}
    }
  return pt_fco;
}

struct TokenDataByName *getTokenDataByName(std::string identifier)
{
  struct TokenDataByName *pt_data = new TokenDataByName;

  LOCK(cs_tally);
  uint32_t nextSPID = _my_sps->peekNextSPID();
  for (uint32_t propertyId = 1; propertyId < nextSPID; propertyId++)
    {
      CMPSPInfo::Entry sp;
      if (_my_sps->getSP(propertyId, sp) && sp.name == identifier)
	{
	  pt_data->data_denominator = sp.denominator;
	  pt_data->data_blocks_until_expiration = sp.blocks_until_expiration;
	  pt_data->data_notional_size = sp.notional_size;
	  pt_data->data_collateral_currency = sp.collateral_currency;
	  pt_data->data_margin_requirement = sp.margin_requirement;
	  pt_data->data_name = sp.name;
	  pt_data->data_subcategory = sp.subcategory;
	  pt_data->data_issuer = sp.issuer;
	  pt_data->data_init_block = sp.init_block;
	  pt_data->data_propertyId = propertyId;
	}
    }
  return pt_data;
}

struct TokenDataByName *getTokenDataById(uint32_t propertyId)
{
  struct TokenDataByName *pt_data = new TokenDataByName;

  LOCK(cs_tally);
  // uint32_t nextSPID = _my_sps->peekNextSPID(1);
  CMPSPInfo::Entry sp;
  if (_my_sps->getSP(propertyId, sp))
	{
	  pt_data->data_denominator = sp.denominator;
	  pt_data->data_blocks_until_expiration = sp.blocks_until_expiration;
	  pt_data->data_notional_size = sp.notional_size;
	  pt_data->data_collateral_currency = sp.collateral_currency;
	  pt_data->data_margin_requirement = sp.margin_requirement;
	  pt_data->data_name = sp.name;
	  pt_data->data_subcategory = sp.subcategory;
	  pt_data->data_issuer = sp.issuer;
	  pt_data->data_init_block = sp.init_block;
	  pt_data->data_propertyId = propertyId;
	}

  return pt_data;
}

/**********************************************************************/
/**Logic for Node Reward**/

void BlockClass::SendNodeReward(std::string sender)
{
  PrintToLog("\nm_BlockInit = %d\t m_BockNow = %s\t sender = %s\n", m_BlockInit, m_BlockNow, sender);

  extern double CompoundRate;
  extern double DecayRate;
  extern double LongTailDecay;

  extern double RewardSecndI;
  extern double RewardFirstI;

  int64_t Reward = 0;

  if (m_BlockNow > m_BlockInit && m_BlockNow <= 100000)
    {
      double SpeedUp = 0.1*pow(CompoundRate, static_cast<double>(m_BlockNow - m_BlockInit));
      Reward = DoubleToInt64(SpeedUp);
      if (m_BlockNow == 100000)
	{
	  mReward.lock();
	  RewardFirstI = Reward;
	  mReward.unlock();
	}
      PrintToLog("\nI1: Reward to Balance = %s\n", FormatDivisibleMP(Reward));
    }
  else if (m_BlockNow > 100000 && m_BlockNow <= 220000)
    {
      double SpeedDw = RewardFirstI*pow(DecayRate, static_cast<double>(m_BlockNow - (m_BlockInit+100000)));
      Reward = DoubleToInt64(SpeedDw);
      if (m_BlockNow == 220000)
	{
	  mReward.lock();
	  RewardSecndI = Reward;
	  mReward.unlock();
	}
      PrintToLog("I2: \nReward to Balance = %s\n", FormatDivisibleMP(Reward));
    }
  else if (m_BlockNow > 220000)
    {
      double SpeedDw = RewardSecndI*pow(LongTailDecay, static_cast<double>(m_BlockNow - (m_BlockInit+220000)));
      Reward = LosingSatoshiLongTail(m_BlockNow, DoubleToInt64(SpeedDw));
      PrintToLog("\nI3: Reward to Balance = %s\n", FormatDivisibleMP(Reward));
    }
}

int64_t LosingSatoshiLongTail(int BlockNow, int64_t Reward)
{
  extern int64_t SatoshiH;
  int64_t RewardH = Reward;

  bool RBool1 = (BlockNow > 220000   && BlockNow <= 720000)   && BlockNow%2 == 0;
  bool RBool2 = (BlockNow > 720000   && BlockNow <= 1500000)  && BlockNow%3 == 0;
  bool RBool3 = (BlockNow > 1500000  && BlockNow <= 7500000)  && BlockNow%4 == 0;
  bool RBool4 = (BlockNow > 7500000  && BlockNow <= 15000000) && BlockNow%5 == 0;
  bool RBool5 = (BlockNow > 15000000 && BlockNow <= 30000000) && BlockNow%6 == 0;

  bool BoolReward = (((RBool1 || RBool2) || RBool3) || RBool4) || RBool5;
  if (BoolReward)
    RewardH -= SatoshiH;

  return RewardH;
}
/**********************************************************************/
