#include "Restarter.h"

#include <iostream>
#include <fstream>
#include <algorithm> 
#include <cctype>
#include <locale>
#include <future>
#include <process.h>
#include <Tlhelp32.h>
#include <winbase.h>

// trim from start (in place)
static inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
        }));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
        }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s) {
    rtrim(s);
    ltrim(s);
}

std::vector<std::string> StrSplit(const std::string& src, const std::string& sep)
{
    std::vector<std::string> r;
    std::string s;
    for (char i : src)
    {
        if (sep.find(i) != std::string::npos)
        {
            if (s.length()) r.push_back(s);
            s.clear();
        }
        else
        {
            s += i;
        }
    }
    if (s.length()) r.push_back(s);
    return r;
}

ClientMonitor::ClientMonitor(std::string accountName, std::string password, std::string charName, ClientManager* mgr, WindowLocation location) : accountName(accountName), password(password),charName(charName), mgr(mgr), location(location)
{
    std::cout << "Adding " << accountName << "(" << charName <<")" << std::endl;

    if (mgr->clientRestartShutdown)
        startTime = 0;
    else
        startTime = time(0);

    lastUpdateTime = time(0);
}

void ClientMonitor::Update(bool setShutdown)
{
    processMutex.lock();

    if (!setShutdown && FindServer() && !FindProcess())
    {
        if (!StartProcess() || !MoveWindow() || !Login())
        {
            std::cout << "Failed to start " << charName << std::endl;
        }
    }
    else
    {
        if (setShutdown || ShouldEnd())
        {
            if (!EndProcess())
            {
                std::cout << "Failed to end " << charName << std::endl;
            }
        }
        else if (!IsOnline() && (!MoveWindow() || !Login()))
        {
            std::cout << "Failed to attach " << charName << std::endl;
        }
        else
        {
            if (time(0) - lastUpdateTime > mgr->ClientMinUpdateDelay)
            {
                SendSequence(mgr->clientAntiAfkSequence);
                lastUpdateTime = time(0);
            }
        }
    }

    processMutex.unlock();
}

bool ClientMonitor::FindServer() const
{
    if (mgr->serverWindowNames.empty())
        return true;

    for (auto windowName : StrSplit(mgr->serverWindowNames, ","))
    {
        if (FindWindowA(mgr->serverWindowClass.c_str(), windowName.c_str()))
            return true;
    }

    return false;   
}

bool ClientMonitor::FindProcess(std::string name, std::string cls)
{
    if (name.empty()) name = accountName;
        processInfo = FindWindowA(cls.c_str(), name.c_str());

    return processInfo;
}

bool ClientMonitor::StartProcess()
{
    const std::string executableName = mgr->clientDefaultWindowName;
    std::cout << "Starting " << accountName << std::endl;
    const std::string executablePath = mgr->clientExecutable;
    const std::string commandLineArgs = mgr->clientArgs;

    // Create a STARTUPINFO structure and set its properties
    STARTUPINFOA startupInfo = { sizeof(startupInfo) };
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOW;

    // Create a PROCESS_INFORMATION structure
    PROCESS_INFORMATION processInformation;

    FindProcess(executableName);

    if (!processInfo)
    {
        // Start the executable
        if (!CreateProcessA(executablePath.c_str(), const_cast<char*>(commandLineArgs.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInformation))
            return false;

        Sleep(2000);

        //Wait for window.
        for (int i = 0; i < 100; i++)
        {
            if (FindProcess(executableName))
                break;

            Sleep(500);

            if (i == 99)
                return false;
        }

        Sleep(500);
    }

    SetWindowTextA(processInfo, accountName.c_str());

    FindProcess();

    //Wait for window to rename.
    for (int i = 0; i < 100; i++)
    {
        if (FindProcess())
            break;

        Sleep(500);

        if (i == 99)
            return false;
    }

    Sleep(500);

    startTime = time(0);

    return true;
}

bool ClientMonitor::MoveWindow()
{
    std::cout << "Resizing " << accountName << std::endl;

    if (!processInfo)
        return false;

    RECT rect;
    GetWindowRect(processInfo, &rect);

    WindowLocation newLoc(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);

    if (location.x != -1)
        newLoc.x = location.x;

    if (location.y != -1)
        newLoc.y = location.y;

    if (location.w != -1)
        newLoc.w = location.w;

    if (location.h != -1)
        newLoc.h = location.h;

    SetWindowPos(processInfo, NULL, newLoc.x, newLoc.y, newLoc.w, newLoc.h, SWP_NOZORDER | SWP_SHOWWINDOW);

    return true;
}

bool ClientMonitor::Login()
{
    std::cout << "Logging into " << accountName << std::endl;

    if (!FindProcess())
        return false;

    SendSequence(mgr->clientLoginSequence);

    Sleep(10000);

    SendSequence(mgr->clientPostLoginSequence);

    return true;
}

bool ClientMonitor::ShouldEnd()
{
    if (!FindProcess())
        return false;

    if (!startTime)
        return true;

    if (!FindServer())
        return true;

    if (mgr->clientAutoShutDownSeconds)
    {
        int timeToEnd = 1000 * mgr->clientAutoShutDownSeconds;

        if (time(0) - startTime > timeToEnd)
            return true;
    }

    if (!IsOnline() && (time(0) - startTime) > 60)
        return true;

    return false;
}

bool ClientMonitor::EndProcess()
{
    if (!FindProcess())
        return true;

    std::cout << "Ending " << accountName << std::endl;

    SendSequence(mgr->clientPreShutdownSequence);

    if (IsOnline())
    {
        SendSequence(mgr->clientOnlineShutdownSequence);
        Sleep(mgr->clientOnlineShutdownSequenceDelay * 1000);
    }

    if (FindProcess())
    {
        //DWORD procesId = GetProcessId(processInfo);
        //HANDLE pHandle = OpenProcess(PROCESS_TERMINATE, 0, procesId);
        //DWORD lpExitCode;        
        //GetExitCodeProcess(pHandle, &lpExitCode);
        //std::cout << "Code =  " << lpExitCode << std::endl;
        TerminateProcess(processInfo, 0);
    }

    return true;
}

void ClientMonitor::SendKeyPress(UINT type, WORD key)
{
    SendMessage(processInfo, type, key, 0x002C0001);
}

void ClientMonitor::SendCharacterKey(WORD key)
{
    PostMessage(processInfo, WM_KEYDOWN, key, 0x002C0001);
    PostMessage(processInfo, WM_CHAR, key, 0x002C0001);
    PostMessage(processInfo, WM_KEYUP, key, 0x002C0001);
}

void ClientMonitor::SendKey(WORD key)
{
    SendKeyPress(WM_KEYDOWN, key);
    SendKeyPress(WM_CHAR, key);
    SendKeyPress(WM_KEYUP, key);
}

void ClientMonitor::SendLine(std::string line)
{
    if (line.find("[SHIFT]") == 0)
    {
        SendKeyPress(WM_KEYDOWN, VK_SHIFT);
        SendLine(line.substr(std::string("[SHIFT]").length()));
        SendKeyPress(WM_KEYUP, VK_SHIFT);
    }
    else if (line.find("[ALT]") == 0)
    {
        SendKeyPress(WM_KEYDOWN, VK_MENU);
        SendLine(line.substr(std::string("[ALT]").length()));
        SendKeyPress(WM_KEYUP, VK_MENU);
    }
    else if (line == "[PAUSE]")
    {
        Sleep(50);
    }
    else if (line.find("[KEY]") == 0)
    {
        SendCharacterKey(line.substr(std::string("[KEY]").size())[0]);
    }
    else if (line == "VK_ESCAPE")
        SendKey(VK_ESCAPE);
    else if (line == "VK_TAB")
        SendKey(VK_TAB);
    else if (line == "VK_RETURN")
        SendKey(VK_RETURN);
    else if (line == "VK_BACK")
        SendKey(VK_BACK);
    else if (line == "VK_F4")
        SendKey(VK_F4);
    else if (line == "<accountname>")
        SendLine(accountName);
    else if (line == "<password>")
        SendLine(password);
    else
    {
        for (auto ch : line)
        {
            SendKey(ch);
        }
    }
}

void ClientMonitor::SendSequence(std::string line)
{
    for (auto& key : StrSplit(line, ","))
    {
        SendLine(key);
        Sleep(50);
    }
}

void ClientManager::Update(bool setShutdown)
{
    CheckOnline();

    for (auto& client : clients)
    {
        if (client->IsDone())
        {
            auto t = std::thread([client, setShutdown] { client->Update(setShutdown); });
            t.detach();
            Sleep(1000);
        }
    }

    Sleep(1000);
}

void ClientManager::LoadConfig()
{
    LoadConfigLines();

    clientDefaultWindowName = GetDefaultString("ClientDefaultWindowName", "client");
    clientExecutable = GetDefaultString("ClientExecutable", "Wow.exe");
    clientArgs = GetDefaultString("clientArgs", "");

    serverWindowNames = GetDefaultString("ServerWindowNames", "server");
    serverWindowClass = GetDefaultString("ServerWindowClass", "ConsoleWindowClass");

    serverOnlineLogLocation = GetDefaultString("ServerOnlineLogLocation", "logs\\player_location.csv");
    serverOnlineLogSeperator = GetDefaultString("ServerOnlineLogSeperator", ",");

    serverOnlineLogSpecifier = GetDefaultString("ServerOnlineLogSpecifier", "PLR");
    serverOnlineLogSpecifierNr = GetDefaultInt("ServerOnlineLogSpecifierNr", 0);
    serverOnlineLogCharNameNr = GetDefaultInt("ServerOnlineLogCharNameNr", 1);

    clientAutoShutDownSeconds = GetDefaultInt("ClientAutoShutDownSeconds", 0);
    clientRestartShutdown = GetDefaultBool("ClientRestartShutdown", 0);

    clientPreShutdownSequence = GetDefaultString("ClientPreShutdownSequence", "");
    clientOnlineShutdownSequence = GetDefaultString("ClientOnlineShutdownSequence", "");
    clientOnlineShutdownSequenceDelay = GetDefaultInt("ClientOnlineShutdownSequenceDelay", 0);

    clientLoginSequence = GetDefaultString("ClientLoginSequence", "");
    clientPostLoginSequence = GetDefaultString("ClientPostLoginSequence", "");
    clientPostLoginDelay = GetDefaultInt("ClientPostLoginDelay", 0);

    clientAntiAfkSequence = GetDefaultString("ClientAntiAfkSequence", "");
    ClientMinUpdateDelay = GetDefaultInt("ClientMinUpdateDelay", 120);

    int DefaultWindowX = GetDefaultInt("DefaultWindowX", -1);
    int DefaultWindowY = GetDefaultInt("DefaultWindowX", -1);
    int DefaultWindowWidth = GetDefaultInt("DefaultWindowWidth", -1);
    int DefaultWindowHeight = GetDefaultInt("DefaultWindowHeight", -1);

    for (int i = 0; i < 1000; i++)
    {
        if (GetDefaultString("Client" + std::to_string(i) + "AccountName", "").empty())
            continue;

        AddAccount(
            GetDefaultString("Client" + std::to_string(i) + "AccountName", "")
            , GetDefaultString("Client" + std::to_string(i) + "Password", GetDefaultString("Client" + std::to_string(i) + "AccountName", ""))
            , GetDefaultString("Client" + std::to_string(i) + "CharacterName", GetDefaultString("Client" + std::to_string(i) + "AccountName", ""))
            , WindowLocation(GetDefaultInt("Client" + std::to_string(i) + "WindowX", DefaultWindowX)
                , GetDefaultInt("Client" + std::to_string(i) + "WindowY", DefaultWindowY)
                , GetDefaultInt("Client" + std::to_string(i) + "WindowWidth", DefaultWindowWidth)
                , GetDefaultInt("Client" + std::to_string(i) + "WindowHeight", DefaultWindowHeight)));
    }
}

void ClientManager::LoadConfigLines()
{
    std::vector<std::string> retVec;

    char result[MAX_PATH];
    GetModuleFileNameA(NULL, result, (sizeof(result)));
    std::string ppath(result);

    ppath = ppath.substr(0, ppath.find_last_of("\\/"));

    std::ifstream in(ppath+"/Restarter.conf", std::ifstream::in);
    if (in.fail())
        return;

    do
    {
        std::string line;
        std::getline(in, line);

        if (!line.length())
            continue;

        if (line.find("#") == 0)
            continue;

        std::vector<std::string> op = StrSplit(line, "=");

        if (op.size() != 2)
            continue;

        trim(op[0]);
        trim(op[1]);

        configLines[op[0]] = op[1];
    } while (in.good());
    in.close();
}

std::vector<std::string> ClientManager::LoadLogFile()
{
    std::vector<std::string> retVec;
    std::ifstream in(serverOnlineLogLocation, std::ifstream::in);
    if (in.fail())
        return {"x"};

    do
    {
        std::string line;
        std::getline(in, line);

        if (!line.length())
            continue;

        std::vector<std::string> tokens = StrSplit(line, serverOnlineLogSeperator);

        if (tokens.size() < 2)
            continue;

        if (serverOnlineLogSpecifierNr && tokens[serverOnlineLogSpecifierNr] != serverOnlineLogSpecifier)
            continue;

        retVec.push_back(line);
    } while (in.good());
    in.close();

    return retVec;
}

void ClientManager::CheckOnline()
{
    std::vector<std::string> log1, log2;

    log1 = LoadLogFile();

    Sleep(500);

    log2 = LoadLogFile();

    if (!log1.empty() && log1[0] == "x")
    {
        for (auto client : clients)
        {
            client->SetOnline(true);
        }
        return;
    }

    if (log1.size() == log2.size())
    {
        for (auto client : clients)
        {
            client->SetOnline(false);
        }

        for (auto& line : log1)
        {
            for (auto& client : clients)
            {
                std::vector<std::string> tokens = StrSplit(line, serverOnlineLogSeperator);

                if (tokens[serverOnlineLogCharNameNr] == client->GetCharName())
                    client->SetOnline(true);
            }
        }
    }
}

int main()
{
    ClientManager mgr;
    bool setShutDown = false;

    mgr.LoadConfig();

    while (true)
    {

        using namespace std::literals;

        // Execute lambda asyncronously.
        auto f = std::async(std::launch::async, [] {
            auto s = ""s;
            if (std::cin >> s) return s; return s;
            });

        while (f.wait_for(2s) != std::future_status::ready) {

            mgr.Update(setShutDown);
        }

        std::string command = f.get();

        if (command == "x")
        {
            setShutDown = true;
        }
        else if (command == "s")
        {
            setShutDown = false;
        }
    }

    return 0;
}
