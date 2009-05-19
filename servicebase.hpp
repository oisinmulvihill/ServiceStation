/*
This is based on the excellent service example from here:

    http://www.ddj.com/cpp/184403531

See License.txt to see what this project is licensed under.

Oisin Mulvihill
2009-04-20

*/
#ifndef _ServiceBase_h_
#define _ServiceBase_h_

#include "stdafx.h"

#define SERVICE_NAME_MAX_LEN 256

// Safe copy up to the max amount we have available or just the length of the
// string id it is less.
void copy_text(char *dest, const char *src, int dest_max, int src_length);

class ServiceBase
{
private:
    bool is_started;
    bool is_paused;

protected:
    char service_name[SERVICE_NAME_MAX_LEN];
    DWORD error_code;

    LPSERVICE_MAIN_FUNCTION service_main;
    LPHANDLER_FUNCTION service_control;

    SERVICE_TABLE_ENTRY dispatch_table[2];
    SERVICE_STATUS service_status;
    SERVICE_STATUS_HANDLE service_stat;

private:
    ServiceBase();               
    ServiceBase(ServiceBase&);   

protected:

    virtual void setAcceptedControls(DWORD controls);
    virtual void changeStatus(
		DWORD state, 
        DWORD checkpoint = (DWORD)0, 
        DWORD waithint = (DWORD)0
	);
    
    virtual DWORD init(DWORD argc, LPTSTR* argv);
    virtual int run(void) = 0;
    
    virtual void installAid(char *exe_path);
    virtual void uninstallAid(void);

    virtual DWORD onPause(void );
    virtual DWORD onContinue(void );

    virtual void onStop(void);
    virtual void onShutdown(void);

    virtual void onInquire(void);
    virtual void onUserControl(DWORD usercmd);

public:
    ServiceBase(
		LPSERVICE_MAIN_FUNCTION service_main, 
		LPHANDLER_FUNCTION service_control
	);
    virtual ~ServiceBase(void);

	int setupFromConfiguration();
	int setupFromConfiguration(const char *config_filename);

	void SetName(std::string new_name);
	const char * GetName(void);

    virtual DWORD Startup(void);
    virtual int Service(DWORD argc, LPTSTR* argv);    
    virtual void control(DWORD opcode);

    virtual bool IsInstalled(void);
    virtual bool Install(void);
    virtual bool UnInstall(void);

    virtual DWORD getLastError(void);    
    virtual DWORD getExitCode(void);    
};

inline DWORD ServiceBase::getExitCode(void)
{
    return this->service_status.dwWin32ExitCode;
}

#endif
