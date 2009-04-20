#include "ServiceBase.hpp"


ServiceBase::ServiceBase(
    LPSERVICE_MAIN_FUNCTION service_main, 
    LPHANDLER_FUNCTION service_control
)
{
	// Store the control function and service main which in a 
	// round-about fashion will refer to Control() and Service()
	//
    this->service_control = service_control;
    this->service_main = service_main;

	// Reset various internal values read to roll:
	//
    memset(this->service_name, 0, sizeof(this->service_name));
    this->is_started = false;
    this->is_paused = false;
    memset(&this->dispatch_table[0], 0, sizeof(this->dispatch_table));
    memset(&this->service_status, 0, sizeof(SERVICE_STATUS));
    this->service_stat = 0;

    this->service_status.dwServiceType = SERVICE_WIN32; 
    this->service_status.dwCurrentState = SERVICE_START_PENDING; 
    this->service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP 
                                            | SERVICE_ACCEPT_PAUSE_CONTINUE
                                            | SERVICE_ACCEPT_SHUTDOWN; 
}

ServiceBase::~ServiceBase( void )
{
}


// Change the current service name:
void ServiceBase::SetName(std::string new_name) 
{
    memset(this->service_name, 0, sizeof(this->service_name));
    strncpy(this->service_name, new_name.c_str(), SERVICE_NAME_MAX_LEN-1);
}


// Recover the current service name:
const char * ServiceBase::GetName(void) 
{
	return (const char *) this->service_name;
}


// Called to start up the service and run.
//
DWORD ServiceBase::Startup(void)
{
	this->dispatch_table[0].lpServiceName = this->service_name;
    this->dispatch_table[0].lpServiceProc = this->service_main;

    if(!StartServiceCtrlDispatcher(this->dispatch_table))
	{
        this->error_code = ::GetLastError();
        return this->error_code;
    }

    return NO_ERROR;
}

// The service main which windows calls when starting the service. The
// first argument should be the name the service was started with.
//
int ServiceBase::Service(DWORD argc, LPTSTR* argv)
{
	std::string service_name = (char *)argv[0];
	this->SetName(service_name);

	// Perform any special actions such as configuration 
	// recovery and service set up before we start running 
	// the service.
	//
    if(Init(argc, argv) != NO_ERROR)
    {
        ChangeStatus(SERVICE_STOPPED);
        return this->error_code;
    }
    
	this->service_stat = RegisterServiceCtrlHandler(_T(this->GetName()), this->service_control);
    if((SERVICE_STATUS_HANDLE)0 == this->service_stat)
	{
        this->error_code = ::GetLastError();
        return this->error_code;
    }
    
    ChangeStatus(SERVICE_RUNNING);
    return Run();
}

// Handle various windows control signals. These will call the various
// methods match the signal i.e. SERVICE_CONTROL_STOP calls OnStop().
//
void ServiceBase::Control(DWORD opcode)
{
    switch(opcode)
    {
    case SERVICE_CONTROL_PAUSE:
        ChangeStatus(SERVICE_PAUSE_PENDING);
        if(OnPause() == NO_ERROR)
        {
            this->is_paused = true;
            ChangeStatus(SERVICE_PAUSED);
        }
        break;

    case SERVICE_CONTROL_CONTINUE:
        ChangeStatus(SERVICE_CONTINUE_PENDING);
        if(OnContinue() == NO_ERROR)
        {
            this->is_paused = false;
            ChangeStatus(SERVICE_RUNNING);
        }
        break;

    case SERVICE_CONTROL_STOP:
        ChangeStatus(SERVICE_STOP_PENDING);
        OnStop();
        ChangeStatus(SERVICE_STOPPED);
        break;

    case SERVICE_CONTROL_SHUTDOWN:
        ChangeStatus(SERVICE_STOP_PENDING);
        OnShutdown();
        ChangeStatus(SERVICE_STOPPED);
        break;

    case SERVICE_CONTROL_INTERROGATE:
        OnInquire();
        SetServiceStatus(this->service_stat, &this->service_status);
        break;

    default:
        OnUserControl(opcode);
        SetServiceStatus(this->service_stat, &this->service_status);
        break;
    };
    return;
}


bool ServiceBase::Install(void)
{
	// Don't reinstall if we've been already been:
    if(IsInstalled())
	{
        return true;
	}
	
    SC_HANDLE service_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if(service_manager == NULL)
    {
        this->error_code = ::GetLastError();
        return false;
    }

	// This is the service exe path and the directory
	// which will be used to run the exe from.
	//
    char file_path[_MAX_PATH];
    ::GetModuleFileName(NULL, file_path, sizeof(file_path));


	// msdn create service ref: 
	//  http://msdn.microsoft.com/en-us/library/ms682450(VS.85).aspx
	//
    SC_HANDLE service = CreateService(
        service_manager,
        this->service_name,
        this->service_name,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        file_path,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );

    bool rc = true;
    if(service == NULL)
    {
        this->error_code = ::GetLastError();
        rc = false;
    }
    else
	{
		// Pass this on so that it can be used in registry set up 
		// if the end user wants to do this.
        InstallAid(file_path);
	}
    
    CloseServiceHandle(service);
    CloseServiceHandle(service_manager);
    return rc;
}

// Remove the service if it is actually present.
bool ServiceBase::UnInstall(void)
{
	// Only do this if it is actually installed!
    if(!IsInstalled())
	{
        return true;
	}

    SC_HANDLE service_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if(service_manager == NULL)
    {
        this->error_code = ::GetLastError();
        return false;
    }

    SC_HANDLE service = OpenService(service_manager, this->service_name, DELETE);
    if(service == NULL)
    {
        this->error_code = ::GetLastError();
        CloseServiceHandle(service_manager);
        return false;
    }

    bool rc = true;
    if(!DeleteService(service))
    {
        rc = false;
        this->error_code = ::GetLastError();
    }

    // Uninstall any registry setup:
    UnInstallAid();

    CloseServiceHandle(service);
    CloseServiceHandle(service_manager);
    return rc;
}

DWORD ServiceBase::GetLastError( void )
{
    return this->error_code;
}

// Check if the service is installed. True indicates it
// has been already. This is used to prevent over writting
// and force removeal before reinstalling it.
//
bool ServiceBase::IsInstalled( void )
{
    bool rc = false;

    // Open the Service Control Manager
    SC_HANDLE service_manager = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (service_manager) 
    {
        // Try to open the service
        SC_HANDLE service = ::OpenService(
			service_manager, 
			this->service_name, 
			SERVICE_QUERY_CONFIG
		);

        if (service) 
        {
            rc = true;
            ::CloseServiceHandle(service);
        }
        ::CloseServiceHandle(service_manager);
    }
    
    return rc;
}

void ServiceBase::SetAcceptedControls(DWORD controls)
{
    this->service_status.dwControlsAccepted = controls;
}

void ServiceBase::ChangeStatus(DWORD state, DWORD checkpoint, DWORD waithint)
{
    this->service_status.dwCurrentState = state;
    this->service_status.dwCheckPoint = checkpoint;
    this->service_status.dwWaitHint = waithint;
    
    SetServiceStatus(this->service_stat, &this->service_status);
}

DWORD ServiceBase::Init(DWORD argc, LPTSTR* argv) 
{ 
	return NO_ERROR; 
}

void  ServiceBase::InstallAid(char * exe_path) 
{
}

void  ServiceBase::UnInstallAid(void) 
{
}

DWORD ServiceBase::OnPause(void) 
{ 
	return NO_ERROR; 
}

DWORD ServiceBase::OnContinue(void) 
{ 
	return NO_ERROR; 
}

void ServiceBase::OnStop(void) 
{
}

void ServiceBase::OnShutdown(void) 
{
	OnStop(); 
}

void  ServiceBase::OnInquire(void) 
{
}

void  ServiceBase::OnUserControl(DWORD) 
{
}


