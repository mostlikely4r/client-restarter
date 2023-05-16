#pragma once
#include <string>
#include <Windows.h>
#include <vector>
#include <mutex>
#include <map>

class ClientManager;

struct WindowLocation
{
	WindowLocation(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {};
	int x, y, w, h;
};

class ClientMonitor
{
public:
	ClientMonitor(std::string accountName, std::string password, std::string charName, ClientManager* mgr, WindowLocation location);

	void Update(bool setShutdown);
	bool IsDone() { bool locked = processMutex.try_lock(); if (locked) processMutex.unlock(); return locked; }
	void SetOnline(bool online) { isOnline = online; }
	std::string GetCharName() { return charName; }
protected:
	bool StartProcess();
	bool MoveWindow();
	bool Login();
	void SendKeyPress(UINT type, WORD key);
	void SendCharacterKey(WORD key);
	void SendKey(WORD key);
	void SendLine(std::string line);
	void SendSequence(std::string line);
	bool ShouldEnd();
	bool EndProcess();

	bool FindServer() const;
	bool FindProcess(std::string name = "",std::string cls = "GxWindowClassD3d");
	bool IsOnline() { return isOnline; }
private:
	HWND processInfo;
	std::string accountName, password, charName;
	WindowLocation location;

	time_t startTime;
	std::mutex processMutex;
	ClientManager* mgr;	
	bool isOnline;
};

class ClientManager
{
public:
	ClientManager() {};
	void AddAccount(std::string accountName, std::string password, std::string charName, WindowLocation location) {clients.push_back(new ClientMonitor(accountName, password, charName, this, location));}
	void Update(bool setShutdown);
	void LoadConfig();
private:
	void LoadConfigLines();

	std::vector<std::string> LoadLogFile();
	void CheckOnline();
	
	std::string GetDefaultString(std::string configName, std::string defaultValue) { if (configLines.find(configName) == configLines.end()) return defaultValue; return configLines[configName]; };
	int GetDefaultInt(std::string configName, int defaultValue) { if (GetDefaultString(configName, "").empty()) return defaultValue; return stoi(GetDefaultString(configName, "")); }
	bool GetDefaultBool(std::string configName, bool defaultValue) {return GetDefaultInt(configName, defaultValue);};

	std::vector<ClientMonitor*> clients;
	std::map<std::string, std::string> configLines;
public:
	std::string clientDefaultWindowName, clientExecutable, clientArgs, serverWindowNames, serverWindowClass, serverOnlineLogLocation, serverOnlineLogSeperator, serverOnlineLogSpecifier, clientPreShutdownSequence, clientOnlineShutdownSequence, clientLoginSequence, clientPostLoginSequence, clientAntiAfkSequence;
	int serverOnlineLogSpecifierNr, serverOnlineLogCharNameNr, clientAutoShutDownSeconds, clientOnlineShutdownSequenceDelay, clientPostLoginDelay;
	bool clientRestartShutdown;
};

