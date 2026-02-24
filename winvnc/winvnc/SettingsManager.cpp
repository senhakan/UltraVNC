// This file is part of UltraVNC
// https://github.com/ultravnc/UltraVNC
// https://uvnc.com/
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SPDX-FileCopyrightText: Copyright (C) 2002-2025 UltraVNC Team Members. All Rights Reserved.
// SPDX-FileCopyrightText: Copyright (C) 1999-2002 Vdacc-VNC & eSVNC Projects. All Rights Reserved.
//


#include "SettingsManager.h"
#include <common/rfb.h>
#include "vncpasswd.h"
#include <VersionHelpers.h>
#include <userenv.h>
#include "credentials.h"
#include <tchar.h>
#include <shlobj.h>
#include <direct.h>
#define SODIUM_STATIC
#include <sodium.h>
#include "common/win32_helpers.h"
#include <fstream>

#pragma comment(lib, "libsodium.lib")

SettingsManager* SettingsManager::s_instance = NULL;
SettingsManager* settings = SettingsManager::getInstance();

SettingsManager* SettingsManager::getInstance()
{
	if (!s_instance) {
		s_instance = new SettingsManager;
	}
	return s_instance;
}

SettingsManager::SettingsManager()
{
	sodium_init();
	m_runtimeDisplayMode = RUNTIME_DISPLAYMODE_NONE;
	m_runtimePortOverrideEnabled = false;
	m_runtimePortOverride = 0;
	m_runtimeEnableNotification = false;
	m_runtimeHideTrayIcon = false;
	m_runtimeConnectionOverlay = false;
	memset(m_runtimeOverlayUser, 0, sizeof(m_runtimeOverlayUser));
	memset(m_runtimeOverlayMessage, 0, sizeof(m_runtimeOverlayMessage));
	setDefaults();
}

static unsigned char hexNibble(char c)
{
	if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
	if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(10 + c - 'a');
	if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(10 + c - 'A');
	return 0;
}

static void loadPasswordFromHex(char* out, size_t outLen, const char* hex)
{
	memset(out, 0, outLen);
	if (!hex) return;

	const size_t pairs = outLen < (strlen(hex) / 2) ? outLen : (strlen(hex) / 2);
	for (size_t i = 0; i < pairs; ++i) {
		const unsigned char hi = hexNibble(hex[i * 2]);
		const unsigned char lo = hexNibble(hex[i * 2 + 1]);
		out[i] = static_cast<char>((hi << 4) | lo);
	}
}

void SettingsManager::Initialize(char *configFile)
{
	UNREFERENCED_PARAMETER(configFile);
	// Embedded profile mode: never load external ini config.
	setDefaults();

	/*HANDLE hPToken = DesktopUsersToken::getInstance()->getDesktopUsersToken();
	int iImpersonateResult = 0;

	if (hPToken != NULL) {
		if (!ImpersonateLoggedOnUser(hPToken)) {
			iImpersonateResult = GetLastError();
			vnclog.Print(LL_INTWARN, VNCLOG("ImpersonateLoggedOnUser failed error %i\n"), iImpersonateResult);
		}
	}

	if (iImpersonateResult == ERROR_SUCCESS)
		RevertToSelf();*/
}

void SettingsManager::setRunningFromExternalService(BOOL fEnabled)
{ 
	m_pref_fRunningFromExternalService = fEnabled; 
};

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")

#include <sddl.h>
#include <wtsapi32.h>

bool SettingsManager::IsDesktopUserAdmin()
{
	HANDLE hPToken = DesktopUsersToken::getInstance()->getDesktopUsersToken();
	if (hPToken) {
		if (!ImpersonateLoggedOnUser(hPToken)) {
			vnclog.Print(LL_LOGSCREEN, "ImpersonateLoggedOnUser Failed");
			vnclog.Print(LL_INTWARN, VNCLOG("ImpersonateLoggedOnUser Failed\n"));
			return false;
		}
	}
	vnclog.Print(LL_LOGSCREEN, "ImpersonateLoggedOnUser OK");
	vnclog.Print(LL_INTWARN, VNCLOG("ImpersonateLoggedOnUser OK\n"));
	bool isAdmin = Credentials::RunningAsAdministrator(RunningFromExternalService());

	if (isAdmin) {
		vnclog.Print(LL_LOGSCREEN, "Desktop user is Administrator");
		vnclog.Print(LL_INTWARN, VNCLOG("Desktop user is Administrator\n"));
	}
	else {
		vnclog.Print(LL_LOGSCREEN, "Desktop user is no Administrator");
		vnclog.Print(LL_INTWARN, VNCLOG("Desktop user is no Administrator\n"));
	}

	RevertToSelf();
	return isAdmin;
}

bool SettingsManager::getAllowUserSettingsWithPassword()
{
	return m_pref_AllowUserSettingsWithPassword;
}

void  SettingsManager::setAllowUserSettingsWithPassword(bool value)
{
	m_pref_AllowUserSettingsWithPassword = value;
}


void SettingsManager::setDefaults()
{
	memset(reinterpret_cast<void*>(m_pref_DSMPluginConfig), 0, sizeof(m_pref_DSMPluginConfig));
#ifdef SC_20
	strcpy_s(m_pref_DSMPluginConfig, "DSMPluginConfig = SecureVNC; 0; 0x00104001;");
#endif // SC_20
	memset(reinterpret_cast<void*>(m_pref_service_commandline), 0, sizeof(m_pref_service_commandline));
	memset(reinterpret_cast<void*>(m_pref_accept_reject_mesg), 0, sizeof(m_pref_accept_reject_mesg));
	memset(reinterpret_cast<void*>(m_pref_passwd), 0, sizeof(m_pref_passwd));
	memset(reinterpret_cast<void*>(m_pref_passwdViewOnly), 0, sizeof(m_pref_passwdViewOnly));
#ifndef SC_20
	memset(reinterpret_cast<void*>(m_pref_szDSMPlugin), 0, sizeof(m_pref_szDSMPlugin));
#else
	strcpy_s(m_pref_szDSMPlugin, "SecureVNCPlugin.dsm");
#endif // SC_20
	
	memset(reinterpret_cast<void*>(m_pref_authhosts), 0, sizeof(m_pref_authhosts));
	strcpy(m_pref_authhosts, "?");

	m_pref_alloweditclients = TRUE;
	m_pref_allowproperties = TRUE;
	m_pref_allowInjection = FALSE;
	m_pref_allowshutdown = TRUE;
	m_pref_ftTimeout = FT_RECV_TIMEOUT;
	m_pref_keepAliveInterval = KEEPALIVE_INTERVAL;
	m_pref_IdleInputTimeout = 0;
	m_pref_Primary = true;
	m_pref_Secondary = false;
#ifndef SC_20
	m_pref_AutoPortSelect = FALSE;
	m_pref_EnableHTTPConnect = FALSE;
	m_pref_PortNumber = 20010;
	m_pref_EnableConnection = TRUE;
	m_pref_HttpPortNumber = DISPLAY_TO_HPORT(PORT_TO_DISPLAY(m_pref_PortNumber));
#else
	m_pref_AutoPortSelect = false;
	m_pref_EnableHTTPConnect = false;
	m_pref_PortNumber = 20010;
	m_pref_EnableConnection = false;
	m_pref_HttpPortNumber = DISPLAY_TO_HPORT(PORT_TO_DISPLAY(m_pref_PortNumber));
#endif // SC_20

	m_pref_QuerySetting = 2;
	m_pref_QueryTimeout = 10;
	m_pref_QueryDisableTime = 0;
	m_pref_QueryAccept = 0;
	m_pref_IdleTimeout = 60;
	m_pref_MaxViewerSetting = 0;
	m_pref_MaxViewers = 128;
	m_pref_EnableRemoteInputs = TRUE;
	m_pref_DisableLocalInputs = FALSE;
	m_pref_EnableJapInput = FALSE;
	m_pref_EnableUnicodeInput = TRUE;
	m_pref_EnableWin8Helper = FALSE;
	m_pref_clearconsole = FALSE;
	m_pref_LockSettings = 0;
	m_pref_Collabo = false;
#ifndef SC_20
	m_pref_Frame = TRUE;
	m_pref_Notification = FALSE;
#else
	m_pref_Frame = true;
	m_pref_Notification = false;
#endif // SC_20
	m_pref_OSD = FALSE;
	m_pref_NotificationSelection = 0;
	m_pref_RemoveWallpaper = FALSE;
	m_pref_RemoveEffects = FALSE;
	m_pref_RemoveFontSmoothing = FALSE;
	m_pref_alloweditclients = TRUE;
	m_pref_allowshutdown = TRUE;
	m_pref_allowproperties = TRUE;
	m_pref_allowInjection = FALSE;
	m_pref_UseDSMPlugin = FALSE;
	m_pref_EnableFileTransfer = TRUE;
	m_pref_FTUserImpersonation = TRUE;
	m_pref_EnableBlankMonitor = TRUE;
	m_pref_BlankInputsOnly = FALSE;
	m_pref_QueryIfNoLogon = 1;
	m_pref_DefaultScale = 1;
	m_pref_RequireMSLogon = false;
	m_pref_Secure = false;
	m_pref_NewMSLogon = false;
#ifdef SC_20
	m_pref_ReverseAuthRequired = false;
#else
	m_pref_ReverseAuthRequired = true;
#endif

	m_pref_DisableTrayIcon = false;
	m_pref_Rdpmode = 0;
	m_pref_Noscreensaver = 0;
#ifdef SC_20
	m_pref_Noscreensaver = 1;	
#endif // SC_20
	m_pref_LoopbackOnly = false;
	m_pref_AllowLoopback = true;
	m_pref_AuthRequired = true;
#ifdef SC_20
	m_pref_AuthRequired = false;
#endif // SC_20
	m_pref_ConnectPriority = 0;

	m_pref_DebugMode = 0;
	strcpy_s(m_pref_DebugPath, "C:\\Users\\hakan.sen\\Downloads\\UltraVNC_1640\\x64");
	m_pref_DebugLevel = 0;
	m_pref_Avilog = 0;
	m_pref_UseIpv6 = 0;
	// ethernet packet 1500 - 40 tcp/ip header - 8 PPPoE info
//unsigned int G_SENDBUFFER=8192;
	G_SENDBUFFER_EX = 1452;

	m_pref_fEnableStateUpdates = false;
	m_pref_fEnableKeepAlive = false;
	m_pref_fRunningFromExternalService = false;
	m_pref_fRunningFromExternalServiceRdp = false;
	m_pref_fAutoRestart = false;
#ifndef SC_20
	m_pref_ScExit = false;
	m_pref_ScPrompt = false;
#else
	m_pref_ScExit = true;
	m_pref_ScPrompt = true;

#endif // SC_20

	m_pref_ddEngine = IsWindows8OrGreater();
	m_pref_TurboMode = TRUE;
	m_pref_PollUnderCursor = FALSE;
	m_pref_PollForeground = FALSE;
	m_pref_PollFullScreen = TRUE;
	m_pref_PollConsoleOnly = FALSE;
	m_pref_PollOnEventOnly = FALSE;
	m_pref_MaxCpu = 100;
	m_pref_MaxFPS = 25;
	m_pref_Driver = FALSE;
	m_pref_Hook = TRUE;
	m_pref_Virtual = FALSE;
	m_pref_autocapt = 1;
	m_pref_ipv6_allowed = false;

	m_pref_RunninAsAdministrator = false;

	memset(reinterpret_cast<void*>(m_pref_group1), 0, sizeof(m_pref_group1));
	memset(reinterpret_cast<void*>(m_pref_group2), 0, sizeof(m_pref_group2));
	memset(reinterpret_cast<void*>(m_pref_group3), 0, sizeof(m_pref_group3));
	m_pref_locdom1 = false;
	m_pref_locdom2 = false;
	m_pref_locdom3 = false;

	memset(m_pref_cloudServer, 0, MAX_HOST_NAME_LEN);
	memset(m_pref_alternateShell, 0, 129);
	m_pref_cloudEnabled = false;
	m_pref_AllowUserSettingsWithPassword = false;
	strcpy_s(m_pref_authhosts, "");
	loadPasswordFromHex(m_pref_passwd, sizeof(m_pref_passwd), "9B436DCF28FDBD8783");
	loadPasswordFromHex(m_pref_passwdViewOnly, sizeof(m_pref_passwdViewOnly), "C80765925ABEBE2AC6");
	applyRuntimeOverrides();

};

void SettingsManager::applyRuntimeOverrides()
{
	switch (m_runtimeDisplayMode) {
	case RUNTIME_DISPLAYMODE_PRIMARY:
		m_pref_Primary = TRUE;
		m_pref_Secondary = FALSE;
		break;
	case RUNTIME_DISPLAYMODE_SECONDARY:
		m_pref_Primary = FALSE;
		m_pref_Secondary = TRUE;
		break;
	case RUNTIME_DISPLAYMODE_ALL:
		m_pref_Primary = TRUE;
		m_pref_Secondary = TRUE;
		break;
	default:
		break;
	}

	if (m_runtimePortOverrideEnabled && m_runtimePortOverride > 0 && m_runtimePortOverride <= 65535) {
		m_pref_AutoPortSelect = FALSE;
		m_pref_PortNumber = m_runtimePortOverride;
		m_pref_HttpPortNumber = DISPLAY_TO_HPORT(PORT_TO_DISPLAY(m_pref_PortNumber));
	}

	if (m_runtimeEnableNotification) {
		m_pref_Notification = TRUE;
	}

	if (m_runtimeHideTrayIcon) {
		m_pref_DisableTrayIcon = TRUE;
	}

	if (m_runtimeConnectionOverlay) {
		m_pref_Frame = TRUE;
		m_pref_OSD = TRUE;
	}
}

void SettingsManager::setRuntimeDisplayModePrimary()
{
	m_runtimeDisplayMode = RUNTIME_DISPLAYMODE_PRIMARY;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeDisplayModeSecondary()
{
	m_runtimeDisplayMode = RUNTIME_DISPLAYMODE_SECONDARY;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeDisplayModeAll()
{
	m_runtimeDisplayMode = RUNTIME_DISPLAYMODE_ALL;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimePortOverride(LONG port)
{
	m_runtimePortOverrideEnabled = (port > 0 && port <= 65535);
	m_runtimePortOverride = port;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeEnableNotification()
{
	m_runtimeEnableNotification = true;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeHideTrayIcon()
{
	m_runtimeHideTrayIcon = true;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeConnectionOverlay()
{
	m_runtimeConnectionOverlay = true;
	applyRuntimeOverrides();
}

void SettingsManager::setRuntimeOverlayUser(const char* user)
{
	memset(m_runtimeOverlayUser, 0, sizeof(m_runtimeOverlayUser));
	memset(m_runtimeOverlayMessage, 0, sizeof(m_runtimeOverlayMessage));

	if (user) {
		strncpy_s(m_runtimeOverlayUser, sizeof(m_runtimeOverlayUser), user, _TRUNCATE);
	}

	if (strlen(m_runtimeOverlayUser) > 0) {
		_snprintf_s(m_runtimeOverlayMessage, sizeof(m_runtimeOverlayMessage), _TRUNCATE,
			"Uzaktan destek aktif! Bagli kullanici: %s", m_runtimeOverlayUser);
	}
}

void SettingsManager::load()
{
	// Embedded profile mode: do not read settings from disk/registry.
	setDefaults();
}

void SettingsManager::savePassword() {
	if (strlen(m_pref_passwd) == 0) {
		iniFile.WriteString("UltraVNC", "passwd", m_pref_passwd);
		return;
	}
	iniFile.WritePassword(m_pref_passwd);
}

void SettingsManager::saveViewOnlyPassword() {
	if (strlen(m_pref_passwdViewOnly) == 0) {
		iniFile.WriteString("UltraVNC", "passwd2", m_pref_passwdViewOnly);
		return;
	}	
	iniFile.WritePasswordViewOnly(m_pref_passwdViewOnly);
}

void SettingsManager::save()
{
	if (!getAllowProperties())
		return;

	// SAVE PER-USER PREFS IF ALLOWED	
	// Modif sf@2002
	iniFile.WriteInt("admin", "AllowUserSettingsWithPassword", m_pref_AllowUserSettingsWithPassword);
	iniFile.WriteInt("admin", "FileTransferEnabled", m_pref_EnableFileTransfer);
	iniFile.WriteInt("admin", "FTUserImpersonation", m_pref_FTUserImpersonation); // sf@2005
	iniFile.WriteInt("admin", "BlankMonitorEnabled", m_pref_EnableBlankMonitor);
	iniFile.WriteInt("admin", "BlankInputsOnly", m_pref_BlankInputsOnly); //PGM
	iniFile.WriteInt("admin", "DefaultScale", m_pref_DefaultScale);
	iniFile.WriteInt("admin", "UseDSMPlugin", m_pref_UseDSMPlugin);
	iniFile.WriteString("admin", "DSMPlugin", m_pref_szDSMPlugin);
	iniFile.WriteString("admin", "DSMPluginConfig", m_pref_DSMPluginConfig);
	iniFile.WriteString("admin", "AuthHosts", m_pref_authhosts);
	iniFile.WriteInt("admin", "primary", m_pref_Primary);
	iniFile.WriteInt("admin", "secondary", m_pref_Secondary);
	iniFile.WriteInt("admin", "SocketConnect", m_pref_EnableConnection);
	iniFile.WriteInt("admin", "HTTPConnect", m_pref_EnableHTTPConnect);
	iniFile.WriteInt("admin", "AutoPortSelect", m_pref_AutoPortSelect);
	if (!m_pref_AutoPortSelect) {
		iniFile.WriteInt("admin", "PortNumber", m_pref_PortNumber);
		iniFile.WriteInt("admin", "HTTPPortNumber", m_pref_HttpPortNumber);
	}
	iniFile.WriteInt("admin", "InputsEnabled", m_pref_EnableRemoteInputs);
	iniFile.WriteInt("admin", "LocalInputsDisabled", m_pref_DisableLocalInputs);
	iniFile.WriteInt("admin", "IdleTimeout", m_pref_IdleTimeout);
	iniFile.WriteInt("admin", "EnableJapInput", m_pref_EnableJapInput);
	iniFile.WriteInt("admin", "EnableUnicodeInput", m_pref_EnableUnicodeInput);
	iniFile.WriteInt("admin", "EnableWin8Helper", m_pref_EnableWin8Helper);
	iniFile.WriteInt("admin", "QuerySetting", m_pref_QuerySetting);
	iniFile.WriteInt("admin", "QueryTimeout", m_pref_QueryTimeout);
	iniFile.WriteInt("admin", "QueryDisableTime", m_pref_QueryDisableTime);
	iniFile.WriteInt("admin", "QueryAccept", m_pref_QueryAccept);
	iniFile.WriteInt("admin", "MaxViewerSetting", m_pref_MaxViewerSetting);
	iniFile.WriteInt("admin", "MaxViewers", m_pref_MaxViewers);
	iniFile.WriteInt("admin", "Collabo", m_pref_Collabo);
	iniFile.WriteInt("admin", "Frame", m_pref_Frame);
	iniFile.WriteInt("admin", "Notification", m_pref_Notification);
	iniFile.WriteInt("admin", "OSD", m_pref_OSD);
	iniFile.WriteInt("admin", "NotificationSelection", m_pref_NotificationSelection);
	iniFile.WriteInt("admin", "QueryIfNoLogon", m_pref_QueryIfNoLogon);
	iniFile.WriteInt("admin", "LockSetting", m_pref_LockSettings);
	iniFile.WriteInt("admin", "RemoveWallpaper", m_pref_RemoveWallpaper);
	iniFile.WriteInt("admin", "RemoveEffects", m_pref_RemoveEffects);
	iniFile.WriteInt("admin", "RemoveFontSmoothing", m_pref_RemoveFontSmoothing);
	iniFile.WriteInt("admin", "DebugMode", vnclog.GetMode());
	iniFile.WriteInt("admin", "Avilog", vnclog.GetVideo());
	iniFile.WriteString("admin", "path", m_pref_DebugPath);
	iniFile.WriteInt("admin", "DebugLevel", vnclog.GetLevel());
	iniFile.WriteInt("admin", "AllowLoopback", m_pref_AllowLoopback);
	iniFile.WriteInt("admin", "UseIpv6", settings->getIPV6());
	iniFile.WriteInt("admin", "LoopbackOnly", m_pref_LoopbackOnly);
	iniFile.WriteInt("admin", "AllowShutdown", m_pref_allowshutdown);
	iniFile.WriteInt("admin", "AllowProperties", m_pref_allowproperties);
	iniFile.WriteInt("admin", "AllowInjection", m_pref_allowInjection);
	iniFile.WriteInt("admin", "AllowEditClients", m_pref_alloweditclients);
	iniFile.WriteInt("admin", "FileTransferTimeout", m_pref_ftTimeout);
	iniFile.WriteInt("admin", "KeepAliveInterval", m_pref_keepAliveInterval);
	iniFile.WriteInt("admin", "IdleInputTimeout", m_pref_IdleInputTimeout);
	iniFile.WriteInt("admin", "DisableTrayIcon", m_pref_DisableTrayIcon);
	iniFile.WriteInt("admin", "rdpmode", m_pref_Rdpmode);
	iniFile.WriteInt("admin", "noscreensaver", m_pref_Noscreensaver);
	iniFile.WriteInt("admin", "Secure", m_pref_Secure);
	iniFile.WriteInt("admin", "MSLogonRequired", m_pref_RequireMSLogon);
	iniFile.WriteInt("admin", "NewMSLogon", m_pref_NewMSLogon);
	iniFile.WriteInt("admin", "ReverseAuthRequired", m_pref_ReverseAuthRequired);
	iniFile.WriteInt("admin", "ConnectPriority", m_pref_ConnectPriority);
	iniFile.WriteInt("admin", "AuthRequired", m_pref_AuthRequired);
	iniFile.WriteString("admin", "service_commandline", m_pref_service_commandline);
	iniFile.WriteString("admin", "accept_reject_mesg", m_pref_accept_reject_mesg);
	iniFile.WriteInt("poll", "TurboMode", m_pref_TurboMode);
	iniFile.WriteInt("poll", "PollUnderCursor", m_pref_PollUnderCursor);
	iniFile.WriteInt("poll", "PollForeground", m_pref_PollForeground);
	iniFile.WriteInt("poll", "PollFullScreen", m_pref_PollFullScreen);
	iniFile.WriteInt("poll", "OnlyPollConsole", m_pref_PollConsoleOnly);
	iniFile.WriteInt("poll", "OnlyPollOnEvent", m_pref_PollOnEventOnly);
	iniFile.WriteInt("poll", "MaxCpu2", m_pref_MaxCpu);
	iniFile.WriteInt("poll", "MaxFPS", m_pref_MaxFPS);
	iniFile.WriteInt("poll", "EnableDriver", m_pref_Driver);
	iniFile.WriteInt("poll", "EnableHook", m_pref_Hook);
	iniFile.WriteInt("poll", "EnableVirtual", m_pref_Virtual);
	iniFile.WriteInt("poll", "autocapt", m_pref_autocapt);

	iniFile.WriteString("admin", "cloudServer", m_pref_cloudServer);
	iniFile.WriteInt("admin", "cloudEnabled", m_pref_cloudEnabled);

	iniFile.WriteString("admin_auth", "group1", m_pref_group1);
	iniFile.WriteString("admin_auth", "group2", m_pref_group2);
	iniFile.WriteString("admin_auth", "group3", m_pref_group3);

	iniFile.WriteInt("admin_auth", "locdom1", m_pref_locdom1);
	iniFile.WriteInt("admin_auth", "locdom2", m_pref_locdom2);
	iniFile.WriteInt("admin_auth", "locdom3", m_pref_locdom3);	
}

void SettingsManager::setkeepAliveInterval(int secs) {
	m_pref_keepAliveInterval = secs;
	if (m_pref_keepAliveInterval >= (m_pref_ftTimeout - KEEPALIVE_HEADROOM))
		m_pref_keepAliveInterval = m_pref_ftTimeout - KEEPALIVE_HEADROOM;
}

void SettingsManager::setIdleTimeout(int secs) {
	m_pref_IdleTimeout = secs;
}

void SettingsManager::setIdleInputTimeout(int secs) {
	m_pref_IdleInputTimeout= secs;
}
static bool notset = false;
bool SettingsManager::IsRunninAsAdministrator()
{
	if (!notset)
		m_pref_RunninAsAdministrator = Credentials::RunningAsAdministrator(RunningFromExternalService());
	notset = true;
	return m_pref_RunninAsAdministrator;
};

bool SettingsManager::checkAdminPassword()
{
	// Embedded profile mode: admin hash is not loaded from external config.
	return false;
}

bool SettingsManager::isAdminPasswordSet()
{
	return false;
}

void SettingsManager::setAdminPasswordHash(char* password)
{
	if (strlen(password) == 0) {
		iniFile.WriteString("UltraVNC", "hash", "");
		return;
	}

	char hashed_password[crypto_pwhash_STRBYTES]{};
	crypto_pwhash_str(
		hashed_password,
		password,
		strlen(password),
		crypto_pwhash_OPSLIMIT_INTERACTIVE,
		crypto_pwhash_MEMLIMIT_INTERACTIVE);

	iniFile.WriteHash(hashed_password, crypto_pwhash_STRBYTES);
}

bool SettingsManager::getShowSettings()
{ 
	return showSettings; 
};

