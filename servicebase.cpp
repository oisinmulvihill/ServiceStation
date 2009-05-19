/*

See License.txt to see what this project is licensed under.

Oisin Mulvihill
2009-04-20

*/
#include "ServiceBase.hpp"

// Copy text safely into a limited amount of space:
void copy_text(char *dest, const char *src, int dest_max, int src_length)
{
	int length = 0;

	// Clear the destination ready for new content:
    ZeroMemory((void *) dest, dest_max);

	if (src_length < dest_max)
	{
		// Copy only the length of the string as there is more then enough
		// space for it.
		length = src_length;
	}
	else
	{
		// Copy only what we can fit taking into account the space needed for 
		// of the null terminator.
		length = dest_max - 1;
	}

	strncpy(dest, src, length);
}


ServiceBase::ServiceBase(
    LPSERVICE_MAIN_FUNCTION service_main, 
    LPHANDLER_FUNCTION service_control
)
{
	// Store the control function and service main which in a 
	// round-about fashion will refer to control() and service()
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

int ServiceBase::setupFromConfiguration()
{
	return 1;
}

int ServiceBase::setupFromConfiguration(const char *config_filename)
{
	return 1;
}

// Change the current service name:
void ServiceBase::setName(std::string new_name) 
{
	copy_text(this->service_name, new_name.c_str(), SERVICE_NAME_MAX_LEN, new_name.length());
}


// Recover the current service name:
const char * ServiceBase::getName(void) 
{
	return (const char *) this->service_name;
}


// Called to start up the service and run.
//
DWORD ServiceBase::startUp(void)
{
	this->dispatch_table[0].lpServiceName = this->service_name;
    this->dispatch_table[0].lpServiceProc = this->service_main;

    if(!StartServiceCtrlDispatcher(this->dispatch_table))
	{
		this->error_code = GetLastError();
        return this->error_code;
    }

    return NO_ERROR;
}

// The service main which windows calls when starting the service. The
// first argument should be the name the service was started with.
//
int ServiceBase::service(DWORD argc, LPTSTR* argv)
{
	std::string service_name = (char *)argv[0];
	this->setName(service_name);

	// Perform any special actions such as configuration 
	// recovery and service set up before we start running 
	// the service.
	//
    if(init(argc, argv) != NO_ERROR)
    {
        changeStatus(SERVICE_STOPPED);
        return this->error_code;
    }
    
	this->service_stat = RegisterServiceCtrlHandler(_T(this->getName()), this->service_control);
    if((SERVICE_STATUS_HANDLE)0 == this->service_stat)
	{
        this->error_code = GetLastError();
        return this->error_code;
    }
    
    changeStatus(SERVICE_RUNNING);
    return run();
}

// Handle various windows control signals. These will call the various
// methods match the signal i.e. SERVICE_CONTROL_STOP calls onStop().
//
void ServiceBase::control(DWORD opcode)
{
    switch(opcode)
    {
    case SERVICE_CONTROL_PAUSE:
        changeStatus(SERVICE_PAUSE_PENDING);
        if(onPause() == NO_ERROR)
        {
            this->is_paused = true;
            changeStatus(SERVICE_PAUSED);
        }
        break;

    case SERVICE_CONTROL_CONTINUE:
        changeStatus(SERVICE_CONTINUE_PENDING);
        if(onContinue() == NO_ERROR)
        {
            this->is_paused = false;
            changeStatus(SERVICE_RUNNING);
        }
        break;

    case SERVICE_CONTROL_STOP:
        changeStatus(SERVICE_STOP_PENDING);
        onStop();
        changeStatus(SERVICE_STOPPED);
        break;

    case SERVICE_CONTROL_SHUTDOWN:
        changeStatus(SERVICE_STOP_PENDING);
        onShutdown();
        changeStatus(SERVICE_STOPPED);
        break;

    case SERVICE_CONTROL_INTERROGATE:
        onInquire();
        SetServiceStatus(this->service_stat, &this->service_status);
        break;

    default:
        onUserControl(opcode);
        SetServiceStatus(this->service_stat, &this->service_status);
        break;
    };
    return;
}


bool ServiceBase::install(void)
{
	// Don't reinstall if we've been already been:
    if(isInstalled())
	{
        return true;
	}
	
    SC_HANDLE service_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if(service_manager == NULL)
    {
        this->error_code = GetLastError();
        return false;
    }

	// This is the service exe path and the directory
	// which will be used to run the exe from.
	//
    char file_path[_MAX_PATH];
    GetModuleFileName(NULL, file_path, sizeof(file_path));


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
        this->error_code = GetLastError();
        rc = false;
    }
    else
	{
		// Pass this on so that it can be used in registry set up 
		// if the end user wants to do this.
        installAid(file_path);
	}
    
    CloseServiceHandle(service);
    CloseServiceHandle(service_manager);
    return rc;
}

// Remove the service if it is actually present.
bool ServiceBase::unInstall(void)
{
	// Only do this if it is actually installed!
    if(!isInstalled())
	{
        return true;
	}

    SC_HANDLE service_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if(service_manager == NULL)
    {
        this->error_code = GetLastError();
        return false;
    }

    SC_HANDLE service = OpenService(service_manager, this->service_name, DELETE);
    if(service == NULL)
    {
        this->error_code = GetLastError();
        CloseServiceHandle(service_manager);
        return false;
    }

    bool rc = true;
    if(!DeleteService(service))
    {
        rc = false;
        this->error_code = GetLastError();
    }

    // Uninstall any registry setup:
    uninstallAid();

    CloseServiceHandle(service);
    CloseServiceHandle(service_manager);
    return rc;
}

DWORD ServiceBase::getLastError( void )
{
    return this->error_code;
}

// Check if the service is installed. True indicates it
// has been already. This is used to prevent over writting
// and force removeal before reinstalling it.
//
bool ServiceBase::isInstalled( void )
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

void ServiceBase::setAcceptedControls(DWORD controls)
{
    this->service_status.dwControlsAccepted = controls;
}

void ServiceBase::changeStatus(DWORD state, DWORD checkpoint, DWORD waithint)
{
    this->service_status.dwCurrentState = state;
    this->service_status.dwCheckPoint = checkpoint;
    this->service_status.dwWaitHint = waithint;
    
    SetServiceStatus(this->service_stat, &this->service_status);
}

DWORD ServiceBase::init(DWORD argc, LPTSTR* argv) 
{ 
	return NO_ERROR; 
}

void  ServiceBase::installAid(char * exe_path) 
{
}

void  ServiceBase::uninstallAid(void) 
{
}

DWORD ServiceBase::onPause(void) 
{ 
	return NO_ERROR; 
}

DWORD ServiceBase::onContinue(void) 
{ 
	return NO_ERROR; 
}

void ServiceBase::onStop(void) 
{
}

void ServiceBase::onShutdown(void) 
{
	onStop(); 
}

void  ServiceBase::onInquire(void) 
{
}

void  ServiceBase::onUserControl(DWORD) 
{
}


