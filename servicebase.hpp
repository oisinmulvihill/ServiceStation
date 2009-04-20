/*
Taken from example service wrapper class http://www.ddj.com/cpp/184403531
*/
#ifndef _ServiceBase_h_
#define _ServiceBase_h_

#include "stdafx.h"

#define SERVICE_NAME_MAX_LEN 256

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

    virtual void SetAcceptedControls(DWORD controls);
    virtual void ChangeStatus(
		DWORD state, 
        DWORD checkpoint = (DWORD)0, 
        DWORD waithint = (DWORD)0
	);
    
    virtual DWORD Init(DWORD argc, LPTSTR* argv);
    virtual int Run(void) = 0;
    
    virtual void InstallAid(char *exe_path);
    virtual void UnInstallAid(void);

    virtual DWORD OnPause(void );
    virtual DWORD OnContinue(void );

    virtual void OnStop(void);
    virtual void OnShutdown(void);

    virtual void OnInquire(void);
    virtual void OnUserControl(DWORD usercmd);

public:
    ServiceBase(
		LPSERVICE_MAIN_FUNCTION service_main, 
		LPHANDLER_FUNCTION service_control
	);
    virtual ~ServiceBase(void);
	
	void SetName(std::string new_name);
	const char * GetName(void);

    virtual DWORD Startup(void);
    virtual int Service(DWORD argc, LPTSTR* argv);    
    virtual void Control(DWORD opcode);

    virtual bool IsInstalled(void);
    virtual bool Install(void);
    virtual bool UnInstall(void);

    virtual DWORD GetLastError(void);    
    virtual DWORD GetExitCode(void);    
};

inline DWORD ServiceBase::GetExitCode(void)
{
    return this->service_status.dwWin32ExitCode;
}

#endif
