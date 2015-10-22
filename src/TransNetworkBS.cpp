#include "TransNetworkBS.h"
#include "MachineInfo.h"
#include "ExternVar.h"
#include "DefStruct.h"

#include <netinet/in.h>
#include <resolv.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include "Util.h"

#include "JSONLib.h"

//#ifdef BS_COMM_MODE

// #define _DEBUG 1

#define HTTP_BUFFER_LEN 4096

#define RES_PARAM_NAME_RESULT							"result"
#define RES_PARAM_VALUE_RESULT_OK	 					"OK"
#define RES_PARAM_VALUE_RESULT_ERROR 					"ERROR"
#define RES_PARAM_VALUE_RESULT_RESET 					"RESET_FK"
#define RES_PARAM_VALUE_RESULT_CANCEL 					"CANCEL_TRANS"


#define ERROR_INVALID_PARAMETER							"ERROR_INVALID_PARAMTER"
#define ERROR_NOT_SUPPORTED								"ERROR_NOT_SUPPORTED"
#define ERROR_NO_ID										"ERROR_NO_ID"
#define ERROR_NO_DATA									"ERROR_NO_DATA"
#define ERROR_UNKNOWN		 							"ERROR_UNKNOWN"

#define BLOCK_LEN 1024

string Trim(const string& str)
{
	string ret;

	string::size_type i, j;
	for(i = 0; i < str.length(); i++)
	{
		if(str[i] == ' ') continue;
		ret += str[i];
	}

	return ret;
}

string GetBSDeviceID()
{
	string ret;
	char vText[32];
	char* vDeviceID = (char*)ghDBAccess.ReadSetupInfo( DI_BS_DEVICE_ID );	
	if(vDeviceID) {
		int vLen = strlen(vDeviceID);
		if(vLen > BS_DEVICE_ID_LENGTH) {
			memcpy(vText, &vDeviceID[vLen - BS_DEVICE_ID_LENGTH], BS_DEVICE_ID_LENGTH);
		} else {
			int v0PadLen = BS_DEVICE_ID_LENGTH - vLen;
			if(v0PadLen) memset(vText, '0', v0PadLen);
			memcpy(vText + v0PadLen, vDeviceID, vLen);
		}
		vText[BS_DEVICE_ID_LENGTH] = 0;
		ret = vText;
	}
	return ret;
}

string GetBSDeviceName()
{
	string ret;
	char* vDeviceName = (char*)ghDBAccess.ReadSetupInfo( DI_BS_DEVICE_NAME );	
	if(vDeviceName)
		ret = vDeviceName;
	return ret;
}


int UserPrivilegeStr2Value( const string aStrPriv )
{
	int vUserPrivilege = MP_NONE;
	if( aStrPriv.compare( "USER" ) == 0 ) vUserPrivilege = MP_NONE;
	else if( aStrPriv.compare( "MANAGER" ) == 0 ) vUserPrivilege = MP_MANAGER_KIND1;
	else if( aStrPriv.compare( "OPERATOR" ) == 0 ) vUserPrivilege = MP_MANAGER_KIND3;
	return vUserPrivilege;
}

string UserPrivilegeValue2Str( int anPriv )
{
	switch(anPriv) {
	case MP_NONE:
		return "USER";
	case MP_MANAGER_KIND1:
		return "MANAGER";
	case MP_MANAGER_KIND3:
		return "OPERATOR";
	}
	return "USER";
}

////////////////////////////////////////////////////////////////////////
/// CHTTPRequest
// 
CHTTPRequest::CHTTPRequest(BOOL aIsPostMode)
{
	m_bIsPost = aIsPostMode;
}

void CHTTPRequest::SetParameter(const string& aStrName, const string& aStrValue)
{
	if(!m_strParameter.empty())
		m_strParameter.append("&"); 
	m_strParameter.append(aStrName);
	m_strParameter.append("=");
	m_strParameter.append(aStrValue);
}

void CHTTPRequest::SetParameter(const string& aStrName, int anValue)
{
	char vText[32];
	sprintf(vText, "%d", anValue);
	SetParameter(aStrName, vText);
}

void CHTTPRequest::SetParameter(const string& aStrName, DWORD anValue)
{
	char vText[32];
	sprintf(vText, "%u", anValue);
	SetParameter(aStrName, vText);
}

void CHTTPRequest::GetCompleted()
{
	char vText[100];
	m_strRequest.clear();

	if(m_bIsPost)
		m_strRequest.append("POST");
	else 
		m_strRequest.append("GET");

	m_strRequest.append(" /fkwebserver/");

	if(!m_bIsPost) {
		if(!m_strParameter.empty()) {
			m_strRequest.append("?");
			m_strRequest.append(m_strParameter);
		}
	}

	m_strRequest.append( " HTTP/1.0\r\n" );
	m_strRequest.append("Accept: image/gif, image/x-xbitmap,"
        " image/jpeg, image/pjpeg, application/vnd.ms-excel,"
        " application/msword, application/vnd.ms-powerpoint,"
        " */*\r\n" );
	
	m_strRequest.append("Accept-Language: en-us\r\n");
	m_strRequest.append("Accept-Encoding: gzip, deflate\r\n");
	m_strRequest.append("User-Agent: Mozilla/4.0\r\n");

	DWORD vIPAddr = 0;
	BOOL vbDHCP = ( ghDBAccess.ReadSetupInfo( DI_USE_ETH_DHCP ) == DEF_USEENABLE );
	if( vbDHCP ) {
		ghDevCtrl.GetNetIpAddress(NIC_ETH_NAME, (unsigned long*)&vIPAddr);
	} else {
		vIPAddr = ghDBAccess.ReadSetupInfo( DI_NETIPADDRESS );
	}
	BYTE* tmp = (BYTE*)&vIPAddr;
	sprintf( vText,  "HOST: %d.%d.%d.%d\r\n", tmp[3], tmp[2], tmp[1], tmp[0] );
	m_strRequest.append( vText );

	if(m_bIsPost)	{
		if(!m_strParameter.empty()) {
			m_strRequest.append("Content-Type: application/x-www-form-urlencoded\r\n");
			sprintf( vText,  "Content-Length: %d\r\n\r\n",  m_strParameter.length());
			m_strRequest.append(vText);
			m_strRequest.append(m_strParameter);
		}
	}
	else {
		m_strRequest.append("\r\n");
	}
	
}

BOOL CHTTPRequest::Execute()
{
	BOOL vReturn = FALSE;
	if(!m_Socket.Open()) return FALSE;

	char vBuf[HTTP_BUFFER_LEN + 1];
	do {
	
		GetCompleted();

		int vRequestLen = m_strRequest.length();
#ifdef _DEBUG
printf("Request Data ==>\n");
printf("%s\n", m_strRequest.data());
printf("<==\n");
#endif
		if(m_Socket.WriteBlock( (char*)m_strRequest.data(), vRequestLen ) != vRequestLen) {
			break;
		}

		char *ptr = vBuf;
		int vLeft = HTTP_BUFFER_LEN;
		do {
			int vRet = m_Socket.Read( ptr, vLeft );
			if(vRet <= 0)
				break;

			ptr += vRet;
			vLeft -= vRet;
		} while(vLeft > 0);

		*ptr = 0;

		m_strResponse = vBuf;
#ifdef _DEBUG
printf("Response Data -->\n");
printf("%s\n", m_strResponse.data());
printf("<--\n");
#endif
		vReturn = TRUE;
	} while(0);

	usleep( 1000 * 30 );

	m_Socket.Close();
	return vReturn;
}


/////////////////////////////////////////////////////////////////////////////////
/// CBSAction
//

BOOL CBSAction::ResponseCommonProc(const string& astrResponse)
{
	string vResValue = CJSONParser::GetParameterToString( astrResponse, RES_PARAM_NAME_RESULT );
	if(vResValue.empty()) return FALSE;

	if(vResValue.find( RES_PARAM_VALUE_RESULT_OK ) != string::npos) 
		return TRUE;

	if(vResValue.compare( RES_PARAM_VALUE_RESULT_RESET ) == 0) {
		/// Add by @pkh 2015.1.4
		char vstrCmd[256];
		sprintf(vstrCmd, "reboot");
		if(!ghDevCtrl.CallExecHelper(vstrCmd))
		{
			ViewDebugInfo("err: reboot fail\n");
		}

		return FALSE;
	}

	if(vResValue.find( RES_PARAM_VALUE_RESULT_ERROR ) != string::npos) 
		return FALSE;

	if(vResValue.compare( RES_PARAM_VALUE_RESULT_CANCEL ) == 0) 
		return FALSE;

	return FALSE;
}

///////////////////////////////////////////////////////////////////////
/// CBSCommandAction
//

BOOL CBSCommandAction::Execute(const string& aStrTransID, const string& aStrCmdParam) 
{
	m_strTransID = aStrTransID;
	m_pTNBS->SetStatus();
}

BOOL CBSCommandAction::SendBigBlockData(
			const string& aStrSendData, const string& aStrFieldName, int anBlkID)
{
	m_pTNBS->SetStatus();

	CHTTPRequest vRequest;
	vRequest.SetParameter( "page_id", "send_cmd_result_big_field" );
	vRequest.SetParameter( "device_id", GetBSDeviceID() );
	vRequest.SetParameter( "device_name", GetBSDeviceName() );
	vRequest.SetParameter( "trans_id", m_strTransID );
	vRequest.SetParameter( "result_field_name", aStrFieldName );
	vRequest.SetParameter( "blk_id", anBlkID );
	vRequest.SetParameter( "blk_data", aStrSendData );

	if( !vRequest.Execute() ) return FALSE;

	string vStrResponse = vRequest.GetResponse();
	if(!ResponseCommonProc(vStrResponse)) {
		return FALSE;
	}

	return TRUE;
}

BOOL CBSCommandAction::RecvBigBlockData(string& aStrRecvData, const string& aStrFieldName)
{
	int vnBlockID = 0;

	do {
		m_pTNBS->SetStatus();

		CHTTPRequest vRequest;
		vRequest.SetParameter( "page_id", "receive_cmd_param_big_field" );
		vRequest.SetParameter( "device_id", GetBSDeviceID() );
		vRequest.SetParameter( "device_name", GetBSDeviceName() );
		vRequest.SetParameter( "trans_id", m_strTransID );
		vRequest.SetParameter( "param_field_name", aStrFieldName );
		vRequest.SetParameter( "blk_id", vnBlockID );

		if( !vRequest.Execute() ) return FALSE;
	
		string vStrResponse = vRequest.GetResponse();
		if(!ResponseCommonProc(vStrResponse)) {
			return FALSE;
		}

		int vnBlockLen = CJSONParser::GetParameterToInt( vStrResponse, "blk_len" );

		string::size_type vnBlockDataIndex = vStrResponse.find( "blk_data" );		
		if( vnBlockDataIndex == string::npos )
			return FALSE;

		vnBlockDataIndex = vStrResponse.find( ":", vnBlockDataIndex );
		if( vnBlockDataIndex == string::npos )
			return FALSE;

		vnBlockDataIndex++;

		aStrRecvData.append( vStrResponse, vnBlockDataIndex, vnBlockLen );

		string vStrResult = CJSONParser::GetParameterToString( vStrResponse, "result" );
		if( vStrResult.compare( "OK_END" ) == 0 )
			break;
		
		vnBlockID++;
	} while(TRUE);

	return TRUE;
}

BOOL CBSCommandAction::SendResultError(const string& aStrError)
{
	m_pTNBS->SetStatus();

	CHTTPRequest vRequest;
	vRequest.SetParameter( "page_id", "send_cmd_result" );
	vRequest.SetParameter( "device_id", GetBSDeviceID() );
	vRequest.SetParameter( "device_name", GetBSDeviceName() );
	vRequest.SetParameter( "trans_id", m_strTransID );
	vRequest.SetParameter( "cmd_code", m_strCmdCode );

	CJSONMaker vResultMaker;
	vResultMaker.SetParameter( "return_code", aStrError );

	string vCmdResult = vResultMaker.GetCompleted();
	vRequest.SetParameter( "cmd_result", vCmdResult);

	if( !vRequest.Execute() ) return FALSE;

	return TRUE;
}

BOOL CBSCommandAction::SendResult(const string& aStrResult)
{
	m_pTNBS->SetStatus();

	CHTTPRequest vRequest;
	vRequest.SetParameter( "page_id", "send_cmd_result" );
	vRequest.SetParameter( "device_id", GetBSDeviceID() );
	vRequest.SetParameter( "device_name", GetBSDeviceName() );
	vRequest.SetParameter( "trans_id", m_strTransID );
	vRequest.SetParameter( "cmd_code", m_strCmdCode );

	vRequest.SetParameter( "cmd_result", aStrResult);

	if( !vRequest.Execute() ) return FALSE;

	string vStrResponse = vRequest.GetResponse();
	if(!ResponseCommonProc(vStrResponse)) {
		return FALSE;
	}

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////
/// CGetEnrollDataAction
///
class CGetEnrollDataAction : public CBSCommandAction
{
public:
	CGetEnrollDataAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_ENROLL_DATA" ) { };

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CGetEnrollDataAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//         cmd_param:{{enroll_id:<1>,backup_number:<2>}}

		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );
		int vBackupNumber = CJSONParser::GetParameterToInt( aStrCmdParam, "backup_number" );

		if(vUserID == 0 ) { 
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}
		
		ENROLLUSERDATA   vtUserData;

		int vnSize = ghDBEnroll.ReadDataByUserID( vUserID, &vtUserData, sizeof(vtUserData) );
		if (vnSize < (long)sizeof(vtUserData)) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}

// 		{return_code:<6>,user_privilege:<7>,face_data:<8>,password:<9>,idcard:<10>}
		CJSONMaker vResultMaker;

		vResultMaker.SetParameter( "return_code", "OK" );
		vResultMaker.SetParameter( "user_privilege", UserPrivilegeValue2Str( vtUserData.Privilege ) );

		switch(vBackupNumber) {
		case BACKUP_PSW:
			vResultMaker.SetParameter( "password", (DWORD)vtUserData.Password );
			break;
		case BACKUP_CARD:
			vResultMaker.SetParameter( "idcard", (DWORD)vtUserData.Card );
			break;
		case BACKUP_FACE:
		{
			int vnBlockID = 0;
			int vnLeftLen = 0;
			int vnDataLen = 0;
			BYTE* vpData = NULL;
			ENROLLFACEDATA vtEnrollFaceData;
			if(ghDevFace.ReadDataByUserID(vUserID, &vtEnrollFaceData, sizeof(vtEnrollFaceData)) < 0) {
				SendResultError( ERROR_NO_DATA );
				return FALSE;
			}
			
			int vnBlockLen = BLOCK_LEN / 2;
			vnLeftLen = sizeof(vtEnrollFaceData.FaceData);
			vpData = (BYTE*)&(vtEnrollFaceData.FaceData);
			do{
				if(vnLeftLen > vnBlockLen) vnDataLen = vnBlockLen;
				else vnDataLen = vnLeftLen;
				
				string vStrData = CJSONMaker::GetByteArrayToString(vpData, vnDataLen);

				if(!SendBigBlockData(vStrData, "face_data", vnBlockID)) {
					SendResultError( ERROR_UNKNOWN );
					return FALSE;
				}

				vpData += vnDataLen;
				vnLeftLen -= vnDataLen;
				vnBlockID ++;

			} while(vnLeftLen);

			vResultMaker.SetParameter( "face_data", "BIG_DATA" );
			break;
		}
		/// { @pkh 2014.9.17
		case BACKUP_FP_0:
		case BACKUP_FP_1:
		case BACKUP_FP_2:
		case BACKUP_FP_3:
		case BACKUP_FP_4:
		case BACKUP_FP_5:
		case BACKUP_FP_6:
		case BACKUP_FP_7:
		case BACKUP_FP_8:
		case BACKUP_FP_9:
		{
			ENROLLFPDATA vtmpFpBuff;

			if( ghDevFinger.ReadEnrollData( vUserID,  vBackupNumber - BACKUP_FP_0 + 1, &vtmpFpBuff, sizeof(vtmpFpBuff) ) != PRO_SUCCESS )
			{
				SendResultError( ERROR_NO_DATA );
				return FALSE;
			}

			vResultMaker.SetParameter( "fp_data", (BYTE*)vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData) );

			break;
		}
		default:
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		/// @pkh 2014.9.17 }
		}

		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CSetEnrollDataAction
///
class CSetEnrollDataAction : public CBSCommandAction
{
public:
	CSetEnrollDataAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "SET_ENROLL_DATA" ) { };

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		ENROLLUSERDATA   vtUserData;
		BOOL vbUpdateFlag = TRUE;

		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CSetEnrollDataAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//         {enroll_id:<5>,backup_number:<6>,user_privilege:<7>,face_data:<8>,password:<9>,idcard:<10>}
		DWORD vnUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );
		int vBackupNumber = CJSONParser::GetParameterToInt( aStrCmdParam, "backup_number" );
		string vstrUserPrivilege = CJSONParser::GetParameterToString( aStrCmdParam, "user_privilege" );

		int vUserPrivilege = UserPrivilegeStr2Value( vstrUserPrivilege );

		if(vnUserID == 0 || vUserPrivilege < MP_NONE || vUserPrivilege > MP_MANAGER_KIND3) {
			SendResultError( ERROR_INVALID_PARAMETER );	
			return FALSE;
		}

		int vnVal = ghDBEnroll.ReadDataByUserID(vnUserID, &vtUserData, sizeof(vtUserData));
		if (vnVal < (int)sizeof(vtUserData))
		{
			vbUpdateFlag = FALSE;
			memset(&vtUserData, 0, sizeof(ENROLLUSERDATA));
	
			vtUserData.UserID = vnUserID;
			vtUserData.E_DeviceID = ghDBAccess.ReadSetupInfo(DI_EXT_DEVICE_ID);
			vtUserData.DataVer = DEF_ENROLLUSER_VER;
			vtUserData.Privilege = MP_NONE;
			vtUserData.Enabled = DEF_USEENABLE;
		}
		vtUserData.Privilege = vUserPrivilege;
	
		BOOL vbNewDataFlag = FALSE;

		switch(vBackupNumber) {
			case BACKUP_PSW: {
				DWORD vnPassword = CJSONParser::GetParameterToDWORD( aStrCmdParam, "password" );
				if( vnPassword == 0 ) {
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}
				if (vtUserData.Password != vnPassword)
				{
					vtUserData.Password = vnPassword;
					vbNewDataFlag = TRUE;
				}
	
				break;
			}
			case BACKUP_CARD: {
				DWORD vnCard = CJSONParser::GetParameterToDWORD( aStrCmdParam, "idcard" );
				if( vnCard == 0 ) {
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}

				if (vtUserData.Card != vnCard)
				{
					vtUserData.Card = vnCard;
					vbNewDataFlag = TRUE;
				}
	
				break;
			}
			case BACKUP_FACE: {
				ENROLLFACEDATA   vtmpFaceBuff;
				string vStrFaceData = CJSONParser::GetParameterToString( aStrCmdParam, "face_data" );
				if( vStrFaceData.compare("BIG_DATA") != 0 ) {
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}

				vStrFaceData.clear();
				if( !RecvBigBlockData(vStrFaceData, "face_data") ) {
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}

				memset(&vtmpFaceBuff, 0, sizeof(vtmpFaceBuff));
				int vnRecvDataLen = sizeof(vtmpFaceBuff.FaceData);
				if( CJSONParser::GetStringToByteArray(vStrFaceData, (BYTE*)&vtmpFaceBuff.FaceData, vnRecvDataLen) != sizeof(vtmpFaceBuff.FaceData) ) {
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}

				vtmpFaceBuff.UserID = vnUserID;
				int vnRecordNum = ghDevFace.FacePutOneDataToDB(&vtmpFaceBuff, sizeof(vtmpFaceBuff), TRUE, FALSE);
				if (vnRecordNum == -1) {
					SendResultError( ERROR_UNKNOWN );
					return FALSE;
				}
				else {
					vtUserData.FaceIndex = vnRecordNum + 1;
					vbNewDataFlag = TRUE;
				}

				break;
			}
			/// { @pkh 2014.9.17
			case BACKUP_FP_0: 
			case BACKUP_FP_1: 
			case BACKUP_FP_2: 
			case BACKUP_FP_3: 
			case BACKUP_FP_4: 
			case BACKUP_FP_5: 
			case BACKUP_FP_6: 
			case BACKUP_FP_7: 
			case BACKUP_FP_8: 
			case BACKUP_FP_9: 
			{
				ENROLLFPDATA vtmpFpBuff;
				int vnRecordNum;
				memset(&vtmpFpBuff, 0, sizeof(vtmpFpBuff));
				if(CJSONParser::GetParameterToBYTEBuf( aStrCmdParam, "fp_data", vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData) ) != sizeof(vtmpFpBuff.FpData)){
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}
				
				vtmpFpBuff.UserID = vnUserID;
				vtmpFpBuff.FpNum = vBackupNumber + 1;

				vnRecordNum = ghDevFinger.WriteEnrollData( vtmpFpBuff.UserID, vtmpFpBuff.FpNum, vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData), TRUE );			
				if (vnRecordNum == -1) {
					SendResultError( ERROR_UNKNOWN );
					return FALSE;				
				}
				vtUserData.FpdataIndex[vBackupNumber] = vnRecordNum + 1;
				vbNewDataFlag = TRUE;
			}
			/// @pkh 2014.9.17 }
			default:
				SendResultError( ERROR_INVALID_PARAMETER );
				return FALSE;
		}

		if (vbNewDataFlag)
		{
			vnVal = ghDBEnroll.WriteEnrollData(&vtUserData, sizeof(vtUserData), vbUpdateFlag, FALSE);
			if (vnVal != sizeof(vtUserData)) {
				SendResultError( ERROR_UNKNOWN );
				return FALSE;
			}
		}

// 		{return_code:<6>}
		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CSetWebServerInfoAction
///
class CSetWebServerInfoAction : public CBSCommandAction
{
public:
	CSetWebServerInfoAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "SET_WEB_SERVER_INFO" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
		
#ifdef _DEBUG
printf("CSetWebServerInfoAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//         {ip_address:<5>,port:<6>}
		DWORD vServerIPAddr = 0;
		string vStrServerIPAddr = CJSONParser::GetParameterToString( aStrCmdParam, "ip_address" );
		DWORD vServerPort = CJSONParser::GetParameterToDWORD( aStrCmdParam, "port" );
		int vTmp[4];
		sscanf(vStrServerIPAddr.data(), "%d.%d.%d.%d", &vTmp[0], &vTmp[1], &vTmp[2], &vTmp[3]);
		
		vServerIPAddr = ((vTmp[0] & 0xFF) << 24) |
						((vTmp[1] & 0xFF) << 16) |  
						((vTmp[2] & 0xFF) << 8) | 
						(vTmp[3] & 0xFF);

		if(vServerIPAddr == 0 || vServerPort == 0) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		ghDBAccess.WriteSetupInfo(DI_SERVERIP, vServerIPAddr);
		ghDBAccess.WriteSetupInfo(DI_SERVERPORT, vServerPort);
		ghDBAccess.SaveSetupInfo();

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;
		
		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CResetFKAction
///
class CResetFKAction : public CBSCommandAction
{
public:
	CResetFKAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "RESET_FK" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);

		/// Add by @pkh 2015.1.4
		char vstrCmd[256];
		sprintf(vstrCmd, "reboot");
		if(!ghDevCtrl.CallExecHelper(vstrCmd))
		{
			ViewDebugInfo("err: reboot fail\n");
			return FALSE;
		}

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CSetTimeAction
///
class CSetTimeAction : public CBSCommandAction
{
public:
	CSetTimeAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "SET_TIME" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
		
#ifdef _DEBUG
printf("CSetTimeAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//			cmd_param:{time:YYYYMMDDHHMMSS}
		int vYear, vMonth, vDay, vHour, vMinute, vSecond;
		string vStrTime = CJSONParser::GetParameterToString( aStrCmdParam, "time" );
		
		sscanf(vStrTime.data(), "%04d%02d%02d%02d%02d%02d", &vYear, &vMonth, &vDay, &vHour, &vMinute, &vSecond);
		if(!ghDevCtrl.SetDateTime(vYear, vMonth, vDay, 0, vHour, vMinute, vSecond)) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}
			
		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CDeleteEnrolleeAction
///
class CDeleteEnrolleeAction : public CBSCommandAction
{
public:
	CDeleteEnrolleeAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "DELETE_ENROLLEE" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
		
#ifdef _DEBUG
printf("CDeleteEnrolleeAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//			 cmd_param:{enroll_id:<5>}
		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );

		if(vUserID == 0) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}
		
		if( !ghDBAccess.DeleteEnrollDataByUserID(vUserID) ) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CRenameEnrolleeAction
///
class CRenameEnrolleeAction : public CBSCommandAction
{
public:
	CRenameEnrolleeAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "RENAME_ENROLLEE" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
		
		char vUserNameBuf[MAX_NAMESIZE * 3 + 1]; // utf8
#ifdef _DEBUG
printf("CRenameEnrolleeAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//			 cmd_param:{enroll_id:<5>,new_name:<6>}
		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );
		memset(vUserNameBuf, 0, sizeof(vUserNameBuf));
		CJSONParser::GetParameterToBYTEBuf( aStrCmdParam, "new_name", (BYTE*)vUserNameBuf, sizeof(vUserNameBuf));

		if(vUserID == 0) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		ENROLLUSERDATA vtEnrollUserData;
		memset( &vtEnrollUserData, 0, sizeof( ENROLLUSERDATA ) );
		
		if( ghDBEnroll.ReadDataByUserID( vUserID, &vtEnrollUserData, sizeof( ENROLLUSERDATA ) ) != sizeof( ENROLLUSERDATA ) ) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}

		PString vStrUserName;
		vStrUserName.SetStringUtf8(vUserNameBuf);

		int vnLen = vStrUserName.GetLength() * 2;
		if( vnLen > sizeof( vtEnrollUserData.Name )- 2 )	vnLen = sizeof(vtEnrollUserData.Name)- 2 ;
		memset( vtEnrollUserData.Name, 0 , sizeof( vtEnrollUserData.Name ) );
		memcpy( vtEnrollUserData.Name, vStrUserName.GetStringUc2(), vnLen );

		int vnRet = ghDBEnroll.WriteEnrollData( &vtEnrollUserData, sizeof(vtEnrollUserData), TRUE );
		if (vnRet != sizeof(vtEnrollUserData)) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CChangeUserPrivilegeAction
///
class CChangeUserPrivilegeAction : public CBSCommandAction
{
public:
	CChangeUserPrivilegeAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "CHANGE_USER_PRIVILEGE" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
		
#ifdef _DEBUG
printf("CChangeUserPrivilegeAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//			 cmd_param:{enroll_id:<5>,user_privilege:<6>}
		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );
		string vstrUserPrivilege = CJSONParser::GetParameterToString( aStrCmdParam, "user_privilege" );

		int vUserPrivilege = UserPrivilegeStr2Value( vstrUserPrivilege );

		if(vUserID == 0 || vUserPrivilege < MP_NONE || vUserPrivilege > MP_MANAGER_KIND3) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}
		
		ENROLLUSERDATA vtEnrollUserData;
		memset( &vtEnrollUserData, 0, sizeof( ENROLLUSERDATA ) );
		
		if( ghDBEnroll.ReadDataByUserID( vUserID, &vtEnrollUserData, sizeof( ENROLLUSERDATA ) ) != sizeof( ENROLLUSERDATA ) ) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}
		vtEnrollUserData.Privilege = vUserPrivilege;

		int vnRet = ghDBEnroll.WriteEnrollData( &vtEnrollUserData, sizeof(vtEnrollUserData), TRUE );
		if (vnRet != sizeof(vtEnrollUserData)) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};


/////////////////////////////////////////////////////////////////////////////////
/// CEnableEnrollFuncAction
///
class CEnableEnrollFuncAction : public CBSCommandAction
{
public:
	CEnableEnrollFuncAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "ENABLE_ENROLL_FUNC" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);

#ifdef _DEBUG
printf("CEnableEnrollFuncAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//         cmd_param:{enable_flag:<5>}
/*<5>은 다음과 같은 문자렬이 될수 있다.
            ON : 사용자등록기능을 활성화시킨다.
            OFF : 사용자등록기능을 리용할수 없게 한다*/
		string vStrEnable = CJSONParser::GetParameterToString( aStrCmdParam, "enable_flag" );
		BOOL vbEnable = FALSE;
		if(vStrEnable.compare("ON") == 0)
			vbEnable = TRUE;
		else
			vbEnable = FALSE;

		ghDBAccess.WriteSetupInfo( DI_USER_ADD_ENABLE, (DWORD)vbEnable );

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CGetEnrollIDListAction
///
class CGetEnrollIDListAction : public CBSCommandAction
{
public:
	CGetEnrollIDListAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_ENROLL_ID_LIST" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CGetEnrollIDListAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif

		int		vnRet, vnRecordNum = 0;
		DWORD	vnUserCount = 0;
		ENROLLUSERDATA vtEnrollData;
		PString vStrUserName;
		CJSONMaker vStrBlockDataMaker;
		int vnBlkID = 0;
		
		vnRecordNum = -1;
		for( vnRecordNum = 0; vnRecordNum < MAX_USERS; vnRecordNum++ ) {
			vnRet = ghDBEnroll.ReadDataByRecordNum( vnRecordNum, &vtEnrollData, sizeof( vtEnrollData ) );
			if( vnRet == PROERR_PROCONTINUE ) continue;
			if( vnRet != PRO_SUCCESS ) {
				break;
			}

//       blk_data  의 격식은 다음과 같다.
//       {{1번째 인원자료},{2번째 인원자료}, 반복 , {마지막 인원자료}}
// 			{enroll_id,name,backup_number,privilege,enable}

			if( vtEnrollData.Password > 0 ) {
				CJSONMaker vOneUserInfoEntryMaker;
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.UserID);
				vStrUserName.SetStringUc2((char*)vtEnrollData.Name);
				vOneUserInfoEntryMaker.SetParameter("", (BYTE*)vStrUserName.GetStringUtf8(), strlen((char*)vStrUserName.GetStringUtf8()));
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)BACKUP_PSW);
				vOneUserInfoEntryMaker.SetParameter("", UserPrivilegeValue2Str( vtEnrollData.Privilege ) );
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.Enabled);

				vStrBlockDataMaker.SetParameter("", vOneUserInfoEntryMaker.GetCompleted());
			}

			if( vtEnrollData.Card > 0 ) {
				CJSONMaker vOneUserInfoEntryMaker;
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.UserID);
				vStrUserName.SetStringUc2((char*)vtEnrollData.Name);
				vOneUserInfoEntryMaker.SetParameter("", (BYTE*)vStrUserName.GetStringUtf8(), strlen((char*)vStrUserName.GetStringUtf8()));
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)BACKUP_CARD);
				vOneUserInfoEntryMaker.SetParameter("", UserPrivilegeValue2Str( vtEnrollData.Privilege ) );
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.Enabled);

				vStrBlockDataMaker.SetParameter("", vOneUserInfoEntryMaker.GetCompleted());
			}

			if( vtEnrollData.FaceIndex > 0 ) {
				CJSONMaker vOneUserInfoEntryMaker;
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.UserID);
				vStrUserName.SetStringUc2((char*)vtEnrollData.Name);
				vOneUserInfoEntryMaker.SetParameter("", (BYTE*)vStrUserName.GetStringUtf8(), strlen((char*)vStrUserName.GetStringUtf8()));
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)BACKUP_FACE);
				vOneUserInfoEntryMaker.SetParameter("", UserPrivilegeValue2Str( vtEnrollData.Privilege ) );
				vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.Enabled);

				vStrBlockDataMaker.SetParameter("", vOneUserInfoEntryMaker.GetCompleted());
			}

			for( int i = 0 ; i < 10 ; i++ )
			{
				if( vtEnrollData.FpdataIndex[i] > 0 ) {
					CJSONMaker vOneUserInfoEntryMaker;
					vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.UserID);
					vStrUserName.SetStringUc2((char*)vtEnrollData.Name);
					vOneUserInfoEntryMaker.SetParameter("", (BYTE*)vStrUserName.GetStringUtf8(), strlen((char*)vStrUserName.GetStringUtf8()));
					vOneUserInfoEntryMaker.SetParameter("", (DWORD)(BACKUP_FP_0 + i));
					vOneUserInfoEntryMaker.SetParameter("", UserPrivilegeValue2Str( vtEnrollData.Privilege ) );
					vOneUserInfoEntryMaker.SetParameter("", (DWORD)vtEnrollData.Enabled);
	
					vStrBlockDataMaker.SetParameter("", vOneUserInfoEntryMaker.GetCompleted());
				}
			}

			if(((vnUserCount % 10) == 9) && vStrBlockDataMaker.GetLength()) { // block send
				string vStrBlockData = vStrBlockDataMaker.GetIncompletedAndClear();
				if( !SendBigBlockData( vStrBlockData, "enroll_id_list", vnBlkID ) ) {
					return FALSE;
				}
				vnBlkID ++;
			}		
			vnUserCount++;
		}

		string vStrLeftData = vStrBlockDataMaker.GetCompleted();
		if( !vStrLeftData.empty() ) {
			if( !SendBigBlockData( vStrLeftData, "enroll_id_list", vnBlkID ) ) {
				return FALSE;
			}
		}

// page_id=send_cmd_result&
// device_id=<1>&
// device_name=<2>&
// trans_id=<3>&
// cmd_code=GET_ENROLL_ID_LIST&
// cmd_result=<5>
// 
//     cmd_result 의 격식
//        {return_code:<6>,enroll_id_count:<7>,
//         enroll_id_struct:<8>,enroll_id_list:BIG_DATA}
// 
//     enroll_id_struct 는 한개 인원자료의 자료구조를 표현하는 문자렬이다.
// 		{enroll_id,name,backup_number,privilege,enable}
		CJSONMaker vResultMaker;
		vResultMaker.SetParameter("return_code", "OK");
		vResultMaker.SetParameter("enroll_id_count", vnUserCount);
		
			CJSONMaker vStructMaker;
			vStructMaker.SetParameter("", "enroll_id");
			vStructMaker.SetParameter("", "name");
			vStructMaker.SetParameter("", "backup_number");
			vStructMaker.SetParameter("", "user_privilege");
			vStructMaker.SetParameter("", "enable");
		
		vResultMaker.SetParameter("enroll_id_struct", vStructMaker.GetCompleted());
		vResultMaker.SetParameter("enroll_id_list", "BIG_DATA");

		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};


/////////////////////////////////////////////////////////////////////////////////
/// CGetLogDataAction
///
class CGetLogDataAction : public CBSCommandAction
{
public:
	CGetLogDataAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_LOG_DATA" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CGetLogDataAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif

// cmd_param  의 격식
//     cmd_param:{begin_time:<6>,end_time:<7>}
//      begin_time, end_time  의 격식은  'YYYYMMDDHHMMSS
		int vBeginYear, vBeginMonth, vBeginDay, vBeginHour, vBeginMinute, vBeginSecond;
		int vEndYear, vEndMonth, vEndDay, vEndHour, vEndMinute, vEndSecond;
		string vStrTime = CJSONParser::GetParameterToString( aStrCmdParam, "begin_time" );
		sscanf(vStrTime.data(), "%04d%02d%02d%02d%02d%02d", &vBeginYear, &vBeginMonth, &vBeginDay, &vBeginHour, &vBeginMinute, &vBeginSecond);
		
		vStrTime = CJSONParser::GetParameterToString( aStrCmdParam, "end_time" );
		sscanf(vStrTime.data(), "%04d%02d%02d%02d%02d%02d", &vEndYear, &vEndMonth, &vEndDay, &vEndHour, &vEndMinute, &vEndSecond);

		ghDBLogData.ReadLogDataStartFromTo( vBeginYear/*y*/, vBeginMonth/*m*/, vBeginDay/*d*/, 
											vEndYear/*y*/, vEndMonth/*m*/, vEndDay/*d*/, 0/*Valid + ReadMark*/ );

		PDate          vtlogDate;
		PTime          vtlogTime;
		DWORD 		vnUserID;
		BYTE  		vnFlag;
		BYTE 		vnVerifyMode;
		BYTE           vnValid;
		char vText[32];
		CJSONMaker vStrBlockDataMaker;
		int vnLogCount = 0;
		int vnBlkID = 0;

		while(1) {
			int vnRet = ghDBLogData.ReadGenLogDataFromDB(
						&vtlogDate, &vtlogTime,
						&vnUserID,
						&vnFlag,
						&vnVerifyMode,
						&vnValid);

			if( vnRet == ERROR_LOG_READ_SUCESS ) {
// {enroll_id,verify_mode,io_mode,io_time}
				CJSONMaker vOneLogEntryMaker;
				vOneLogEntryMaker.SetParameter("", vnUserID);
/// { @pkh 2015.03.25 for bug fix
/*				vOneLogEntryMaker.SetParameter("", vnVerifyMode);
				vOneLogEntryMaker.SetParameter("", vnFlag);
*/
				vOneLogEntryMaker.SetParameter( "", GetLogVerifyMode2LogString(vnVerifyMode) );
				vOneLogEntryMaker.SetParameter( "", GetLogIOMode2LogString(vnFlag) );
/// } @pkh 2015.03.25 for bug fix
				sprintf(vText, "%04d%02d%02d%02d%02d%02d", 
							vtlogDate.Year(), vtlogDate.Month(), vtlogDate.Day(), 
							vtlogTime.Hour(), vtlogTime.Minute(), vtlogTime.Second());
				vOneLogEntryMaker.SetParameter("", vText);

				vStrBlockDataMaker.SetParameter("", vOneLogEntryMaker.GetCompleted());
			}
			else {
				break;
			}

			if((vnLogCount % 20 == 19) && vStrBlockDataMaker.GetLength()) { // block send
				if( !SendBigBlockData( vStrBlockDataMaker.GetIncompletedAndClear(), "log_data", vnBlkID ) ) {
					ghDBLogData.ReadLogDataEnd();
					return FALSE;
				}

				vnBlkID++;
			}		

			vnLogCount++;
		}
		ghDBLogData.ReadLogDataEnd();

		string vStrData = vStrBlockDataMaker.GetCompleted();
		if( !vStrData.empty() ) {
			if( !SendBigBlockData( vStrData, "log_data", vnBlkID ) ) {
				return FALSE;
			}
		}

// page_id=send_cmd_result&
// device_id=<1>&
// device_name=<2>&
// trans_id=<3>&
// cmd_code=GET_LOG_DATA&
// cmd_result=<5>
// 
// cmd_result  의 격식
//     {return_code:<6>,log_type:<7>,log_count:<8>,
//       log_data_struct:<9>,log_data:BIG_DATA}
// 
//     log_data_struct 은 하나의 로그자료의 격식을 나타내는 문자렬이다.
//     그 구체적인 값은 다음과 같다.
//       {enroll_id,verify_mode,io_mode,io_time}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter("return_code", "OK");
		vResultMaker.SetParameter("log_type", 0 );
		vResultMaker.SetParameter("log_count", vnLogCount);
		
			CJSONMaker vStructMaker;
			vStructMaker.SetParameter("", "enroll_id");
			vStructMaker.SetParameter("", "verify_mode");
			vStructMaker.SetParameter("", "io_mode");
			vStructMaker.SetParameter("", "io_time");
		
		vResultMaker.SetParameter("enroll_id_struct", vStructMaker.GetCompleted());
		vResultMaker.SetParameter("log_data", "BIG_DATA");

		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CEnableEnrolleeAction
///
class CEnableEnrolleeAction : public CBSCommandAction
{
public:
	CEnableEnrolleeAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "ENABLE_ENROLLEE" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);

#ifdef _DEBUG
printf("CEnableEnrolleeAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
//     cmd_param:{enroll_id:<5>,enable_flag:<6>}
// 
//       enable_flag 은 다음과 같은 값을 가질수 있다.
//         ON : 등록된 사용자가 해당 기대를 사용할수 있게 한다.
//         OFF : 등록된 사용자가 해당 기대를 사용할수 없게 한다.

		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );
		string vStrEnabled = CJSONParser::GetParameterToString( aStrCmdParam, "enable_flag" );

		if(vUserID == 0) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		ENROLLUSERDATA vtEnrollUserData;
		memset( &vtEnrollUserData, 0, sizeof( ENROLLUSERDATA ) );
		
		if( ghDBEnroll.ReadDataByUserID( vUserID, &vtEnrollUserData, sizeof( ENROLLUSERDATA ) ) != sizeof( ENROLLUSERDATA ) ) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}

		if(vStrEnabled.compare("ON") == 0)
			vtEnrollUserData.Enabled = DEF_USEENABLE;
		else
			vtEnrollUserData.Enabled = DEF_USEDISABLE;

		int vnRet = ghDBEnroll.WriteEnrollData( &vtEnrollUserData, sizeof(vtEnrollUserData), TRUE );
		if (vnRet != sizeof(vtEnrollUserData)) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CSetFKNameAction
///
class CSetFKNameAction : public CBSCommandAction
{
public:
	CSetFKNameAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "SET_FK_NAME" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CSetFKNameAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//   cmd_param  의 격식
//     cmd_param:{fk_name:<5>}
// 		fk_name은 반드시 영문자이여야 한다.

		string vStrName = CJSONParser::GetParameterToString( aStrCmdParam, "fk_name" );

		if(vStrName.empty()) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		ghDBAccess.WriteSetupInfo(DI_BS_DEVICE_NAME, (DWORD)vStrName.data());

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CClearLogDataAction
///
class CClearLogDataAction : public CBSCommandAction
{
public:
	CClearLogDataAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "CLEAR_LOG_DATA" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CClearLogDataAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif

		if( !ghDBLogData.ClearLogData() ) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CClearEnrollDataAction
///
class CClearEnrollDataAction : public CBSCommandAction
{
public:
	CClearEnrollDataAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "CLEAR_ENROLL_DATA" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CClearEnrollDataAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif

		if( !ghDBAccess.AllClearEnrollData() ) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CGetLogCountAction
///
class CGetLogCountAction : public CBSCommandAction
{
public:
	CGetLogCountAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_LOG_COUNT" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);

#ifdef _DEBUG
printf("CGetLogCountAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
		int vnLogCount = ghDBLogData.GetAllLogCount();

//  cmd_result  의 격식
// {return_code:<6>,log_count:<7>}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		vResultMaker.SetParameter( "log_count", vnLogCount );

		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};


/////////////////////////////////////////////////////////////////////////////////
/// CGetEnrolleeCountAction
///
class CGetEnrolleeCountAction : public CBSCommandAction
{
public:
	CGetEnrolleeCountAction( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_ENROLLEE_COUNT" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CGetEnrolleeCountAction::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
		DWORD vUserCnt, vManagerCnt, vFaceCnt, vFpCnt, vPassCnt, vCardCnt;
		ghDBAccess.GetSystemStatus(STATUS_MANAGERS, &vManagerCnt);
		ghDBAccess.GetSystemStatus(STATUS_ALLUSERS, &vUserCnt);
		
		ghDBAccess.GetSystemStatus(STATUS_FACES, &vFaceCnt);
		ghDBAccess.GetSystemStatus(STATUS_FPS, &vFpCnt); /// @pkh 2014.9.17
		ghDBAccess.GetSystemStatus(STATUS_PSWS, &vPassCnt);
		ghDBAccess.GetSystemStatus(STATUS_CARDS, &vCardCnt);

//  cmd_result  의 격식
// 	{return_code:<6>,manager_count:<7>,user_count:<8>,
//       fp_count:<9>,password_count:<10>,idcard_count:<11>}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		vResultMaker.SetParameter( "manager_count", vManagerCnt );
		vResultMaker.SetParameter( "user_count", vUserCnt );
		vResultMaker.SetParameter( "face_count", vFaceCnt );
		vResultMaker.SetParameter( "fp_count", vFpCnt ); /// @pkh 2014.9.17
		vResultMaker.SetParameter( "password_count", vPassCnt );
		vResultMaker.SetParameter( "idcard_count", vCardCnt );

		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CSetEnrollData2Action
///
class CSetEnrollData2Action : public CBSCommandAction
{
public:
	CSetEnrollData2Action( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "SET_ENROLL_DATA_2" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);

#ifdef _DEBUG
printf("CSetEnrollData2Action::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//   cmd_param의 격식
//         {enroll_data:BIG_DATA}
		
		string vStrDataType = CJSONParser::GetParameterToString( aStrCmdParam, "enroll_data" );
		if(vStrDataType.compare("BIG_DATA") != 0) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		string vStrEnrollData;
		if( !RecvBigBlockData(vStrEnrollData, "enroll_data") ) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

	//  {enroll_id:<6>,name:<7>,privilege:<13>,fp_count:<8>,fp0:<9>,fp1:<10>,…,password:<11>,idcard:<12>}
		ENROLLFACEDATA   vtmpFaceBuff;
		char vUserNameBuf[MAX_NAMESIZE * 3 + 1]; // utf8
		DWORD vnUserID =  CJSONParser::GetParameterToDWORD( vStrEnrollData, "enroll_id" );
		memset(vUserNameBuf, 0, sizeof(vUserNameBuf));
		int vnUserNameLen = CJSONParser::GetParameterToBYTEBuf( vStrEnrollData, "name", (BYTE*)vUserNameBuf, sizeof(vUserNameBuf));
		string vstrPrivilege =  CJSONParser::GetParameterToString( vStrEnrollData, "user_privilege" );
		DWORD vnPassword =  CJSONParser::GetParameterToDWORD( vStrEnrollData, "password" );
		DWORD vnCard =  CJSONParser::GetParameterToDWORD( vStrEnrollData, "idcard" );

		memset(&vtmpFaceBuff, 0, sizeof(vtmpFaceBuff));
		int vnFaceDataLen = CJSONParser::GetParameterToBYTEBuf( vStrEnrollData, "face_data", vtmpFaceBuff.FaceData, sizeof(vtmpFaceBuff.FaceData) );

		int vnPrivilege = UserPrivilegeStr2Value( vstrPrivilege );
/// { @pkh 2015.03.25 for bug fix
// 		if( vnUserID == 0 || vnPrivilege < MP_NONE || vnPrivilege > MP_MANAGER_KIND3 || vnFaceDataLen != sizeof(vtmpFaceBuff.FaceData) ) {
		if( vnUserID == 0 || vnPrivilege < MP_NONE || vnPrivilege > MP_MANAGER_KIND3 ) {
/// } @pkh 2015.03.25 for bug fix
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		ENROLLUSERDATA   vtUserData;
		int vnVal = ghDBEnroll.ReadDataByUserID(vnUserID, &vtUserData, sizeof(vtUserData));
		if (vnVal < (int)sizeof(vtUserData))
		{
			memset(&vtUserData, 0, sizeof(ENROLLUSERDATA));
	
			vtUserData.UserID = vnUserID;
			vtUserData.E_DeviceID = ghDBAccess.ReadSetupInfo(DI_EXT_DEVICE_ID);
			vtUserData.DataVer = DEF_ENROLLUSER_VER;
			vtUserData.Privilege = MP_NONE;
			vtUserData.Enabled = DEF_USEENABLE;
		}
		vtUserData.Privilege = vnPrivilege;

		if(vnUserNameLen) {
			PString vStrUserName;
			vStrUserName.SetStringUtf8(vUserNameBuf);
	
			int vnLen = vStrUserName.GetLength() * 2;
			if( vnLen > sizeof( vtUserData.Name )- 2 )	vnLen = sizeof(vtUserData.Name)- 2 ;
			memset( vtUserData.Name, 0 , sizeof( vtUserData.Name ) );
			memcpy( vtUserData.Name, vStrUserName.GetStringUc2(), vnLen );

		}

		if(vnPassword) {
			vtUserData.Password = vnPassword;
		}

		if(vnCard) {
			vtUserData.Card = vnCard;
		}

		int vnRecordNum;
		if(vnFaceDataLen) {
/// { @pkh 2015.03.25 for bug fix
			if(vnFaceDataLen < sizeof(vtmpFaceBuff.FaceData))
			{
				SendResultError( ERROR_INVALID_PARAMETER );
				return FALSE;
			}
/// } @pkh 2015.03.25 for bug fix
			vtmpFaceBuff.UserID = vnUserID;
			vnRecordNum = ghDevFace.FacePutOneDataToDB(&vtmpFaceBuff, sizeof(vtmpFaceBuff), TRUE, FALSE);
			if (vnRecordNum == -1) {
				SendResultError( ERROR_UNKNOWN );
				return FALSE;				
			}
			vtUserData.FaceIndex = vnRecordNum + 1;
		}

		/// { @pkh 2014.9.17
		int vnFpCount = CJSONParser::GetParameterToInt( vStrEnrollData, "fp_count" );
		
		int i, vnFpDataLen;
		char vFpDataName[255];
		ENROLLFPDATA vtmpFpBuff;

		for(i = 0 ; i < 10 ; i++ )
		{
			sprintf(vFpDataName, "fp%d", i);
			memset(&vtmpFpBuff, 0, sizeof(vtmpFpBuff));
			vnFpDataLen = CJSONParser::GetParameterToBYTEBuf( vStrEnrollData, vFpDataName, vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData) );
			
			if(vnFpDataLen)
			{
/// { @pkh 2015.03.25 for bug fix
				if(vnFpDataLen < sizeof(vtmpFpBuff.FpData))
				{
					SendResultError( ERROR_INVALID_PARAMETER );
					return FALSE;
				}
/// } @pkh 2015.03.25 for bug fix
				vtmpFpBuff.UserID = vnUserID;
				vtmpFpBuff.FpNum = i + 1;

				vnRecordNum = ghDevFinger.WriteEnrollData( vtmpFpBuff.UserID, vtmpFpBuff.FpNum, vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData), TRUE );			
				if (vnRecordNum < 0) {
					SendResultError( ERROR_UNKNOWN );
					return FALSE;				
				}
				vtUserData.FpdataIndex[i] = vnRecordNum + 1;
			}
		}
		/// @pkh 2014.9.17 }

		vnVal = ghDBEnroll.WriteEnrollData(&vtUserData, sizeof(vtUserData), TRUE, FALSE);
		if (vnVal != sizeof(vtUserData)) {
			SendResultError( ERROR_UNKNOWN );
			return FALSE;
		}

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CGetEnrollData2Action
///
class CGetEnrollData2Action : public CBSCommandAction
{
public:
	CGetEnrollData2Action( CTransNetworkBS *apTNBS ) : CBSCommandAction( apTNBS, "GET_ENROLL_DATA_2" ) {};

	virtual BOOL Execute(const string& aStrTransID, const string& aStrCmdParam) {
		CBSCommandAction::Execute(aStrTransID, aStrCmdParam);
#ifdef _DEBUG
printf("CGetEnrollData2Action::Execute()================================> aStrCmdParam =%s\n", aStrCmdParam.data());
#endif
//     cmd_param의 격식
// 			cmd_param:{enroll_id:<5>}

		DWORD vUserID = CJSONParser::GetParameterToDWORD( aStrCmdParam, "enroll_id" );

		if(vUserID == 0 ) {
			SendResultError( ERROR_INVALID_PARAMETER );
			return FALSE;
		}

		CJSONMaker vEnrollDataMaker;
		ENROLLUSERDATA   vtUserData;

		int vnSize = ghDBEnroll.ReadDataByUserID( vUserID, &vtUserData, sizeof(vtUserData) );
		if (vnSize < (long)sizeof(vtUserData)) {
			SendResultError( ERROR_NO_ID );
			return FALSE;
		}

//       {enroll_id:<6>,name:<7>,user_privilege:<12>,fp_count:<8>,fp0:<9>, … ,password<10>,idcard:<11>}

		vEnrollDataMaker.SetParameter( "enroll_id", vUserID );

		PString vStrUserName;
		vStrUserName.SetStringUc2((char*)vtUserData.Name);
		vEnrollDataMaker.SetParameter( "name", (BYTE*)vStrUserName.GetStringUtf8(), strlen((char*)vStrUserName.GetStringUtf8()) );
		vEnrollDataMaker.SetParameter( "user_privilege", UserPrivilegeValue2Str( vtUserData.Privilege ) );
		vEnrollDataMaker.SetParameter( "password", (DWORD)vtUserData.Password );
		vEnrollDataMaker.SetParameter( "idcard", (DWORD)vtUserData.Card );

		ENROLLFACEDATA vtEnrollFaceData;
		if(ghDevFace.ReadDataByUserID(vUserID, &vtEnrollFaceData, sizeof(vtEnrollFaceData)) >= 0)
			vEnrollDataMaker.SetParameter( "face_data", (BYTE*)vtEnrollFaceData.FaceData, sizeof(vtEnrollFaceData.FaceData) );

		/// { @pkh 2014.9.17
		int vnFpCount = 0;
		int i;
		char vFpDataName[255];
		ENROLLFPDATA vtmpFpBuff;

		for(i = 0 ; i < 10 ; i++ )
		{
			if( ghDevFinger.ReadEnrollData( vUserID, (WORD)i + 1, &vtmpFpBuff, sizeof(vtmpFpBuff) ) != PRO_SUCCESS )
				continue;

			sprintf(vFpDataName, "fp%d", i);
			vEnrollDataMaker.SetParameter( vFpDataName, (BYTE*)vtmpFpBuff.FpData, sizeof(vtmpFpBuff.FpData) );

			vnFpCount ++;
		}
		if( vnFpCount > 0 )
			vEnrollDataMaker.SetParameter( "fp_count", vnFpCount );

		/// @pkh 2014.9.17 }

		string vStrEnrollData = vEnrollDataMaker.GetCompleted();

		int vnBlockID = 0;
		int vnLeftLen = 0;
		int vnDataLen = 0;
		char vStrBlockData[BLOCK_LEN + 1];
		char* vpData = (char*)vStrEnrollData.data();
		
		vnLeftLen = vStrEnrollData.length();
		do{
			if(vnLeftLen > BLOCK_LEN) vnDataLen = BLOCK_LEN;
			else vnDataLen = vnLeftLen;

			memcpy(vStrBlockData, vpData, vnDataLen);
			vStrBlockData[vnDataLen] = 0;
			if(!SendBigBlockData(vStrBlockData, "enroll_data", vnBlockID)) {
				SendResultError( ERROR_UNKNOWN );
				return FALSE;
			}

			vpData += vnDataLen;
			vnLeftLen -= vnDataLen;
			vnBlockID ++;

		} while(vnLeftLen);

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "return_code", "OK" );
		vResultMaker.SetParameter( "enroll_id", vUserID );
		vResultMaker.SetParameter( "enroll_data", "BIG_DATA" );
		string vCmdResult = vResultMaker.GetCompleted();
		if( !SendResult( vCmdResult ) )
			return FALSE;

		return TRUE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CReceiveCommandAction
///
class CReceiveCommandAction : public CBSAction
{
public:
	CReceiveCommandAction( CTransNetworkBS *apTNBS ) : CBSAction( apTNBS ) {};

	virtual BOOL Execute() {
		char vText[100];
		DWORD vUserCnt, vManagerCnt, vFaceCnt, vFpCnt, vPassCnt, vCardCnt, vLogCnt;
		PDateTime vDateTime;
		PString vStrFirmware;
		PString vStrProVersion;

		ghDBAccess.GetSystemStatus(STATUS_MANAGERS, &vManagerCnt);
		ghDBAccess.GetSystemStatus(STATUS_ALLUSERS, &vUserCnt);
		
		ghDBAccess.GetSystemStatus(STATUS_FACES, &vFaceCnt);
		ghDBAccess.GetSystemStatus(STATUS_FPS, &vFpCnt); /// @pkh 2014.9.17
		ghDBAccess.GetSystemStatus(STATUS_PSWS, &vPassCnt);
		ghDBAccess.GetSystemStatus(STATUS_CARDS, &vCardCnt);
		vLogCnt = ghDBLogData.GetAllLogCount();
	
		ghDBAccess.GetSystemStatus(STATUS_FIRMWARE, NULL, vText);
		vStrFirmware.SetStringUtf8(vText);
	
		ghDBAccess.GetSystemStatus(STATUS_PRO_VERSION, NULL, vText);
		vStrProVersion.SetStringUtf8(vText);
	
		ghDevCtrl.GetCurrentDateTime(&vDateTime);
	
		CHTTPRequest vRequest;
		vRequest.SetParameter( "page_id", "receive_cmd" );
		vRequest.SetParameter( "device_id", GetBSDeviceID() );
		vRequest.SetParameter( "device_name", GetBSDeviceName() );
	
		sprintf(vText, "%04d%02d%02d%02d%02d%02d", 
						vDateTime.Year(), vDateTime.Month(), vDateTime.Day(), 
						vDateTime.Hour(), vDateTime.Minute(), vDateTime.Second());
		vRequest.SetParameter( "device_time", vText );

		/// { @pkh 2014.9.17
/*		sprintf(vText, "{enrollee:%d,face:%d,pwd:%d,idcard:%d,manager:%d,log:%d,firmware:%s,fk_type:%s}", 
								vUserCnt, vFaceCnt, vPassCnt, vCardCnt, vManagerCnt, vLogCnt,
								vStrFirmware.GetStringUtf8(), vStrProVersion.GetStringUtf8());*/
		sprintf(vText, "{enrollee:%d,face:%d,fp:%d,pwd:%d,idcard:%d,manager:%d,log:%d,firmware:%s,fk_type:%s}", 
								vUserCnt, vFaceCnt, vFpCnt, vPassCnt, vCardCnt, vManagerCnt, vLogCnt,
								vStrFirmware.GetStringUtf8(), vStrProVersion.GetStringUtf8());
		/// @pkh 2014.9.17 }
		vRequest.SetParameter( "device_info", Trim(vText) );
		
		if(!vRequest.Execute())
			return FALSE;

		string vStrResponse = vRequest.GetResponse();
		if(!ResponseCommonProc(vStrResponse)) {
			return FALSE;
		}
		
		string vStrCmdCode = CJSONParser::GetParameterToString( vStrResponse, "cmd_code" );
		string vStrTransID = CJSONParser::GetParameterToString( vStrResponse, "trans_id" );
		string vStrCmdParam = CJSONParser::GetParameterToString( vStrResponse, "cmd_param" );

		CBSCommandAction* vpCmdAction = NULL;
		if(vStrCmdCode.compare("GET_ENROLL_DATA") == 0) {
			vpCmdAction = new CGetEnrollDataAction( m_pTNBS );
		} else if(vStrCmdCode.compare("SET_ENROLL_DATA") == 0) {
			vpCmdAction = new CSetEnrollDataAction( m_pTNBS );
		} else if(vStrCmdCode.compare("SET_WEB_SERVER_INFO") == 0) {
			vpCmdAction = new CSetWebServerInfoAction( m_pTNBS );
		} else if(vStrCmdCode.compare("RESET_FK") == 0) {
			vpCmdAction = new CResetFKAction( m_pTNBS );
		} else if(vStrCmdCode.compare("SET_TIME") == 0) {
			vpCmdAction = new CSetTimeAction( m_pTNBS );
		} else if(vStrCmdCode.compare("DELETE_ENROLLEE") == 0) {
			vpCmdAction = new CDeleteEnrolleeAction( m_pTNBS );
		} else if(vStrCmdCode.compare("RENAME_ENROLLEE") == 0) {
			vpCmdAction = new CRenameEnrolleeAction( m_pTNBS );
		} else if(vStrCmdCode.compare("CHANGE_USER_PRIVILEGE") == 0) {
			vpCmdAction = new CChangeUserPrivilegeAction( m_pTNBS );
		} else if(vStrCmdCode.compare("ENABLE_ENROLL_FUNC") == 0) {
			vpCmdAction = new CEnableEnrollFuncAction( m_pTNBS );
		} else if(vStrCmdCode.compare("GET_ENROLL_ID_LIST") == 0) {
			vpCmdAction = new CGetEnrollIDListAction( m_pTNBS );
		} else if(vStrCmdCode.compare("GET_LOG_DATA") == 0) {
			vpCmdAction = new CGetLogDataAction( m_pTNBS );
		} else if(vStrCmdCode.compare("ENABLE_ENROLLEE") == 0) {
			vpCmdAction = new CEnableEnrolleeAction( m_pTNBS );
		} else if(vStrCmdCode.compare("SET_FK_NAME") == 0) {
			vpCmdAction = new CSetFKNameAction( m_pTNBS );
		} else if(vStrCmdCode.compare("CLEAR_LOG_DATA") == 0) {
			vpCmdAction = new CClearLogDataAction( m_pTNBS );
		} else if(vStrCmdCode.compare("CLEAR_ENROLL_DATA") == 0) {
			vpCmdAction = new CClearEnrollDataAction( m_pTNBS );
		} else if(vStrCmdCode.compare("GET_LOG_COUNT") == 0) {
			vpCmdAction = new CGetLogCountAction( m_pTNBS );
		} else if(vStrCmdCode.compare("GET_ENROLLEE_COUNT") == 0) {
			vpCmdAction = new CGetEnrolleeCountAction( m_pTNBS );
		} else if(vStrCmdCode.compare("SET_ENROLL_DATA_2") == 0) {
			vpCmdAction = new CSetEnrollData2Action( m_pTNBS );
		} else if(vStrCmdCode.compare("GET_ENROLL_DATA_2") == 0) {
			vpCmdAction = new CGetEnrollData2Action( m_pTNBS );
		} else {
			printf("unkown command = %s\n", vStrCmdCode.data());
			return FALSE;
		} 

		if(vpCmdAction) {
			BOOL vReturn = vpCmdAction->Execute( vStrTransID, vStrCmdParam );
			delete vpCmdAction;
			return vReturn;
		}
		return FALSE;
	}
};

/////////////////////////////////////////////////////////////////////////////////
/// CRTLogSendAction
///
class CRTLogSendAction : public CBSAction
{
public:
	CRTLogSendAction( CTransNetworkBS *apTNBS ) : CBSAction( apTNBS ) {};

	virtual BOOL Execute() {
		char vText[100];
		DWORD vUserCnt, vManagerCnt, vFaceCnt, vPassCnt, vCardCnt, vLogCnt;
		PDateTime vDateTime;
		PString vStrFirmware;
		PString vStrProVersion;

		ghDBAccess.GetSystemStatus(STATUS_MANAGERS, &vManagerCnt);
		ghDBAccess.GetSystemStatus(STATUS_ALLUSERS, &vUserCnt);
		
		ghDBAccess.GetSystemStatus(STATUS_FACES, &vFaceCnt);
		ghDBAccess.GetSystemStatus(STATUS_PSWS, &vPassCnt);
		ghDBAccess.GetSystemStatus(STATUS_CARDS, &vCardCnt);
		vLogCnt = ghDBLogData.GetAllLogCount();
	
		ghDBAccess.GetSystemStatus(STATUS_FIRMWARE, NULL, vText);
		vStrFirmware.SetStringUtf8(vText);
	
		ghDBAccess.GetSystemStatus(STATUS_PRO_VERSION, NULL, vText);
		vStrProVersion.SetStringUtf8(vText);
	
		ghDevCtrl.GetCurrentDateTime(&vDateTime);
	
		CHTTPRequest vRequest;
		vRequest.SetParameter( "page_id", "realtime_glog" );
		vRequest.SetParameter( "device_id", GetBSDeviceID() );
		vRequest.SetParameter( "device_name", GetBSDeviceName() );

		CJSONMaker vResultMaker;
		vResultMaker.SetParameter( "enroll_id", m_LogData.UserID );
		vResultMaker.SetParameter( "verify_mode", GetLogVerifyMode2LogString(m_LogData.VerifyMode) );
		vResultMaker.SetParameter( "io_mode", GetLogIOMode2LogString(m_LogData.FlagResult) );
		
		sprintf(vText, "%04d%02d%02d%02d%02d%02d", 
						m_LogData.Year + 1900, m_LogData.Month, m_LogData.Day, 
						m_LogData.Hour, m_LogData.Minute, m_LogData.Second);
		vResultMaker.SetParameter( "io_time", vText );

		string vStrLogData = vResultMaker.GetCompleted();
		vRequest.SetParameter( "glog_data", vStrLogData );
		
		if(!vRequest.Execute())
			return FALSE;

		string vStrResponse = vRequest.GetResponse();
		if(!ResponseCommonProc(vStrResponse)) {
			return FALSE;
		}
		
		return TRUE;
	}

	GENERALLOGDATA m_LogData;
};


CTransNetworkBS::CTransNetworkBS()
{
	m_nRecvSize = 0;
	m_FlagReceiveData = FALSE;
	m_bLogSendNotify		 = FALSE;

}


CTransNetworkBS::~CTransNetworkBS()
{
}


BOOL CTransNetworkBS::Start( void )
{	
	m_FlagWhile = TRUE;
	if( pthread_create( &m_Handle, NULL, &CTransNetworkBS::Run, this ) != 0 ) {
		m_FlagWhile = FALSE;
		return FALSE;
	}
	return TRUE;
}

BOOL CTransNetworkBS::End( void )
{
	void* vpData;
	
	pthread_join( m_Handle, &vpData );
	if( vpData != NULL ) free( vpData );
	
	m_FlagWhile = FALSE;
	
	return TRUE;
}

void* CTransNetworkBS::Run( void* apInst )
{
	CTransNetworkBS* vpThis = static_cast<CTransNetworkBS*>(apInst);
	vpThis->ProcTrans();
}

BOOL CTransNetworkBS::GetStatus( void )
{
	BOOL vRet = m_FlagReceiveData;
	m_FlagReceiveData = FALSE;
	return vRet;
}

void CTransNetworkBS::SetStatus()
{
	m_FlagReceiveData = TRUE;
}

#define SLEEP_TIME 100 // ms

BOOL CTransNetworkBS::SendRTLog()
{
	GENERALLOGDATA vLogData;
	sqlite3_int64	vnLogID;

// 	char vLogImage[5 * 1024];
	int vnLogImageLen = 0/*sizeof(vLogImage)*/;

	CRTLogSendAction vRTLogSendAction(this);

	do {

		if(!ghDBLogData.ReadRTLogData(&vnLogID, &vLogData, NULL, &vnLogImageLen )) {
			m_bLogSendNotify = FALSE;
			return FALSE;
		}
	
		memcpy(&vRTLogSendAction.m_LogData, &vLogData, sizeof(GENERALLOGDATA));
	
		if( !vRTLogSendAction.Execute() ) {
			m_bLogSendNotify = FALSE;
			return FALSE;
		}
	
		ghDBLogData.DeleteRTLogData(vnLogID);
	
		usleep( 1000 * SLEEP_TIME );

	} while(TRUE);

	m_bLogSendNotify = FALSE;

	return TRUE;
}

BOOL CTransNetworkBS::ProcTrans( void )
{
	while( 1 ) {
		if ( m_FlagWhile == FALSE ) return FALSE;

		SendRTLog();

		usleep( 1000 * SLEEP_TIME );

		CReceiveCommandAction vRecvCmdRequest(this);
		if( !vRecvCmdRequest.Execute() ) {
			DWORD vnTime = ghDBAccess.ReadSetupInfo( DI_SERVER_RESPONSE_TIME );
			int vSleepCount = (vnTime * 1000) / SLEEP_TIME;
			for(int i = 0; i < vSleepCount; i++) {
				if(m_bLogSendNotify) break;
				usleep( 1000 * SLEEP_TIME );
			}
			
			continue;
		}

		usleep( 1000 * SLEEP_TIME );
	}
}

//#endif /*BS_COMM_MODE*/






