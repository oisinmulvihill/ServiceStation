/*

See License.txt to see what this project is licensed under.

Oisin Mulvihill
2009-04-20

*/
#include "SimpleIni.h"

#include "service.hpp"


Service::Service(
		std::string config_file, 
		LPSERVICE_MAIN_FUNCTION  service_main, 
		LPHANDLER_FUNCTION service_control
	)
    : ServiceBase(service_main, service_control)
{
	int rc = 0;

	// Zero storeage:
	ZeroMemory(registry_path, sizeof(registry_path));
	ZeroMemory(process_name, sizeof(process_name));
	ZeroMemory(log_file_name, sizeof(log_file_name));
	ZeroMemory(working_path, sizeof(working_path));


	// Redirecting stdout/stderror based on the MSDN
	// article here:
	//
	//	http://msdn.microsoft.com/en-us/library/ms682499(VS.85).aspx 
	//
    this->childStd_ERR_Read = NULL;
	this->childStd_ERR_Write = NULL;
	this->childStd_OUT_Read = NULL;
	this->childStd_OUT_Write = NULL;
	this->childStd_OUT_tmp = NULL;
	this->log_file = NULL;

	this->service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP 
		                                    | SERVICE_ACCEPT_SHUTDOWN;

	// Set up the config file and path and then read in the 
	// configuration for the rest of the service setup:
	//
	copy_text(this->config_file, config_file.c_str(), NAME_PATH_MAX_LENGTH, config_file.length());

	// Used by startProcess...
	//
	this->process_info = NULL;
}

Service::~Service( void )
{
	// Child process cleanup:
	if (this->process_info)
	{
		// Close process and thread handles. 
		CloseHandle(this->process_info->hProcess);
	    CloseHandle(this->process_info->hThread);
		delete this->process_info;
	}

	// Logging clean up:
	if (this->log_file)
	{
		CloseHandle(this->log_file);
	}

	// Close the job process cleanly, all process should have stopped already.
	if (this->job_processes)
	{
		CloseHandle(this->job_processes);
	}
}


// Log and event to the windows event log. This should show up under
// application section of the EventViewer. The level indicates the
// nature of the message informational, error, warning, etc. This can
// be one of S_INFO, S_WARN, S_ERROR. The default is S_INFO.
//
// If the configuration hasn't been setup yet for a various reasons,
// then messages will appear under the default source 'ServiceRunner'.
//
void Service::logEvent(const char *message, int level) 
{
	int rc = 0;

	// Have a look at MSDN http://msdn.microsoft.com/en-us/library/aa363679(VS.85).aspx
	// ReportEvent to see the detailed evt_type value information. The evt_type
	// can be one of:
	//
	// EVENTLOG_SUCCESS, EVENTLOG_AUDIT_FAILURE, EVENTLOG_AUDIT_SUCCESS,
	// EVENTLOG_ERROR_TYPE, EVENTLOG_INFORMATION_TYPE, EVENTLOG_WARNING_TYPE
	//
	// The default is EVENTLOG_INFORMATION_TYPE.
	//
	// MSDN example: http://msdn.microsoft.com/en-us/library/aa363680(VS.85).aspx
	//
	WORD evt_type = EVENTLOG_SUCCESS;

	// Not sure what to do with this (Event Categories):
	// MSDN Ref: http://msdn.microsoft.com/en-us/library/aa363649(VS.85).aspx
	//
	WORD category = 1;

	// Not sure what to do with this as well. 
	// Ref: MSDN Event Identifiers:
	// http://msdn.microsoft.com/en-us/library/aa363651(VS.85).aspx
	//
	DWORD event_id = 1;

	// Set up the windows side of error/info/warn/etc.
	switch (level) 
	{
		case S_INFO:
			evt_type = EVENTLOG_INFORMATION_TYPE;
			break;

		case S_WARN:
			evt_type = EVENTLOG_WARNING_TYPE;
			break;

		case S_ERROR:
			evt_type = EVENTLOG_ERROR_TYPE;
			break;

		default:
			evt_type = EVENTLOG_INFORMATION_TYPE;
			break;
	}

	HANDLE event_source = RegisterEventSource(NULL, this->GetName());      
	if(event_source == NULL) {
		std::cerr << "Unable to register source: '" << this->GetName() << "'." << std::endl;     
	}
	else
	{
		rc = ReportEvent(
			event_source,               
			evt_type,
			category,               
			event_id, 
			NULL,      
			1,              
			strlen(message),  
			&message,   
			(void*)message
		);

		if (!rc) {
			std::cout << "Unable to log event: '" << message << "'." << std::endl;     
		}

		DeregisterEventSource(event_source);     
	}
}


// Load the configuration file recovered from the command line
// or via the registry. The construct or init() will call this
// method. The file will return NO_ERROR if everything is ok.
//
int Service::setupFromConfiguration(void)
{
	return this->setupFromConfiguration(this->config_file);
}

int Service::setupFromConfiguration(const char *config_filename)
{
	bool IsUtf8 = TRUE;
	bool UseMultiKey = FALSE;
	bool UseMultiLine = FALSE;

	CSimpleIniA ini(IsUtf8, UseMultiKey, UseMultiLine);

	SI_Error rc = ini.LoadFile(config_filename);
	if (rc < 0) 
	{
		char pTemp[MAX_PATH + 255] = "";
		sprintf(pTemp, "Unable to load configuration from: '%s'.", config_filename);
		this->logEvent(pTemp, S_ERROR);
		return 1;
	}

	// Create the process job which we'll use to contain our processes in:
	// ref: http://msdn.microsoft.com/en-us/library/ms684161.aspx
	//
	this->job_processes = CreateJobObject(NULL, "servicestation-jobs");
	if (!(this->job_processes))
	{
		this->logEvent("Error creating service station job container!", S_ERROR);
		return 1;
	}

	// Set up the name of this service:
	//
	std::string service_name = ini.GetValue("service", "name", "ServiceStation");
	this->SetName(service_name);

	// Set the service description based on what we find in the config file:
	//
	std::string description = ini.GetValue("service", "description", "ServiceStation Managed Service");
	this->setDescription(description);

	// Get the GUI flag indicating desktop interaction:
	//
	this->has_gui = ini.GetValue("service", "gui", "no");
	if (this->has_gui == "yes") 
	{
		this->interactiveState(true);
		this->logEvent("This service has the GUI flag set (Desktop Interaction).", S_INFO);		
	}
	else
	{
		this->has_gui = "no";
		this->interactiveState(false);
		this->logEvent("The service has no desktop interaction flag set.", S_INFO);
	}

	// Set up the command which is to be run as a service:
	//
	std::string command_line = ini.GetValue("service", "command_line", "cmd.exe");
	if (command_line.length() < 1)
	{
		this->logEvent("Error command_line was an empty string!", S_ERROR);
		return 1;
	}
	copy_text(this->process_name, command_line.c_str(), NAME_PATH_MAX_LENGTH, command_line.length());


	// Set up where the process is run from:
	//
	std::string working_dir = ini.GetValue("service", "working_dir", "c:\\");
	copy_text(this->working_path, working_dir.c_str(), NAME_PATH_MAX_LENGTH, working_dir.length());

	// The file to write the child processes STDOUT/ERR to:
	//
	std::string the_log_file = ini.GetValue("service", "log_file", "child_out_err.log");
	copy_text(this->log_file_name, the_log_file.c_str(), MAX_PATH, the_log_file.length());

	//this->log_file = CreateFile(
	//   (LPCTSTR) (log_file_name), 
	//   GENERIC_READ | GENERIC_WRITE, 
	//   0, 
	//   NULL, 
	//   CREATE_NEW, 
	//   FILE_ATTRIBUTE_NORMAL, 
	//   NULL
	//); 

	//if (this->log_file == INVALID_HANDLE_VALUE) 
	//{
	//  this->log_file = NULL;
	//  char pTemp[MAX_PATH + 255] = "";
	//  sprintf(pTemp, "Unable to access/read: '%s'.", this->log_file_name);
 //     this->logEvent(pTemp, S_ERROR);
	//  //
	//  //return 1;
	//}	

	return NO_ERROR;
}


// Recover the specific setup for this service and load this 
// config file, setting up this service instance. The service 
// exe path is the basis for distinguising multiple instances.
// Putting the exe in a new directory with the same/different
// config will allow you to run another service instance.
//
DWORD Service::init(DWORD ac, LPTSTR *av)
{
	DWORD rc = 0;

	// Recover the service exe path and then open the registry
	// key for this so we can then load our configuration.
	//
    char szFilePath[_MAX_PATH];
    ::GetModuleFileName(NULL, szFilePath, sizeof(szFilePath));

	char reg_start[] = "SOFTWARE\\StationService\\Services\\";
	int reg_start_length = strlen(reg_start);
	char reg_end[] = "\\setup";
	int reg_end_length = strlen(reg_start);
	int max_exe_length = REG_PATH_MAX_LENGTH - (reg_start_length+reg_end_length);
	memset(&(this->registry_path[0]), 0, REG_PATH_MAX_LENGTH);

	// Before setting up the registry key check we can fit in the space for it:
	//
	if (_MAX_PATH >= max_exe_length) 
	{
		char pTemp[255] = "";
		sprintf(pTemp, "Service::init(): Cannot recover the registry as the exe path and name are too large!");
		this->logEvent(pTemp, S_ERROR);
		return 1;
	}
	strcpy(registry_path, reg_start);	
	strcat(registry_path, szFilePath);							
	strcat(registry_path, reg_end);								

	HKEY service_key;
	 
	rc = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE, 
		registry_path, 
		0, 
		KEY_READ,
		&service_key 
	);
	if (rc == ERROR_SUCCESS) 
	{
        // Set the config file this service must use when it starts up.
		// The config file is the absolute path and filename we must use.
		// No relative paths are that will more then likely fail.
		//
		DWORD keytype;
		char data[REG_PATH_MAX_LENGTH] = "";
		DWORD len = REG_PATH_MAX_LENGTH;

		rc = RegQueryValueEx(
			service_key,
			"config_file",
			NULL,
			&keytype,
			(BYTE*)&data,
			&len
		);

		if (rc != ERROR_SUCCESS)
	    {
			this->logEvent("Service::init(): Could get the config file value from registry!", S_INFO);		
			return 1;
		}
		else
		{
			rc = this->setupFromConfiguration(data);
		}
        RegCloseKey(service_key);
	}
	else
	{
		char pTemp[REG_PATH_MAX_LENGTH + 255] = "";
		sprintf(pTemp, "Service::init(): Unable to open registry key '%s'!", registry_path);
		this->logEvent(pTemp, S_ERROR);
		return 1;
	}

    return rc;
}

int Service::run( void )
{
	this->is_running = true;
	this->startProcess();

    while(this->is_running)
	{
        // Monitor the sub-process and restart it if it has stopped:
		DWORD dwCode;

		// Alternative? Wait until child process exits.
		//WaitForSingleObject( this->process_info->hProcess, INFINITE );
		if(::GetExitCodeProcess(this->process_info->hProcess, &dwCode) && this->process_info->hProcess != NULL)
		{
			if(dwCode != STILL_ACTIVE)
			{
				if(this->startProcess())
				{
					this->logEvent("run: Restarted process ok.\n", S_WARN);
				}
			}
		}
		else 
		{
			long nError = getLastError();
			char pTemp[1024];
			sprintf(pTemp,"Service::run: unable to start service! Error code = %d\n", nError); 
			this->logEvent(pTemp, S_ERROR);
		}

		// Log any child process console activity to the log file:
		//
		//this->readWriteOutErrFromPipe();

		// Wait a bit so we're not hogging CPU time too much.
		Sleep(1000);
	}
    
	// Ok, time to exit tell out child process to stop as well.
	this->stopProcess();

	return NO_ERROR; 
}

// Set description
bool Service::setDescription(std::string description)
{
    bool rc = false;
	SERVICE_DESCRIPTION sd;


    // Open the Service Control Manager
    SC_HANDLE service_manager = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (service_manager) 
    {
        // Try to open the service
        SC_HANDLE service = ::OpenService(
			service_manager, 
			this->service_name, 
			SERVICE_CHANGE_CONFIG
		);
        if (service) 
        {
			// Changing service config, ref:
			//    http://msdn.microsoft.com/en-us/library/ms682006(VS.85).aspx
			//
			char szDesc[SERVICE_DESC_MAX_LENGTH];
		
			copy_text(szDesc, description.c_str(), SERVICE_DESC_MAX_LENGTH, description.length());
			sd.lpDescription = szDesc;

			char pTemp[SERVICE_DESC_MAX_LENGTH + 255] = "";
			sprintf(pTemp, "Service::setDescription(): set to '%s'!", description.c_str());
			this->logEvent(pTemp, S_WARN);

			// Now attempt to change the service type:
			rc = ChangeServiceConfig2(
				service,
				SERVICE_CONFIG_DESCRIPTION,
				&sd
			);

            ::CloseServiceHandle(service);
        }
        ::CloseServiceHandle(service_manager);
    }
    
    return rc;
}


// Enable or disable the interaction with the desktop. To
// enable interaction the flag is true is pass to this 
// method. To disable then the string "off" is passed instead
// If the operation was successfull true will be returned
// otherwise false will indicate an error.
//
bool Service::interactiveState(bool interactive_state)
{
    bool rc = false;

	// Set up with default no interaction:
	DWORD service_type = SERVICE_WIN32_OWN_PROCESS;

    // Open the Service Control Manager
    SC_HANDLE service_manager = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (service_manager) 
    {
        // Try to open the service
        SC_HANDLE service = ::OpenService(
			service_manager, 
			this->service_name, 
			SERVICE_CHANGE_CONFIG
		);
        if (service) 
        {
			if (interactive_state)
			{
				// set up interactive flags:
				service_type = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
				this->logEvent("interactiveState: ON.", S_INFO);
			}
			else
			{
				this->logEvent("interactiveState: OFF.", S_INFO);
			}

			// Now attempt to change the service type:
			rc = ChangeServiceConfig(
				service,
				service_type,
				SERVICE_NO_CHANGE,
				SERVICE_NO_CHANGE,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL
			);

            ::CloseServiceHandle(service);
        }
        ::CloseServiceHandle(service_manager);
    }
    
    return rc;
}


// Called by windows to stop the service running.
//
void Service::onStop( void )
{
	this->logEvent("Service::onStop - Exit time", S_INFO);
	this->is_running = false;
	this->stopProcess();
}


// Make an attempt to start the child process. If this fails we'll
// be called again by run() when it detects the child process is
// no longer running.
//
bool Service::startProcess()
{
	bool job_assign = FALSE;
	bool rc = FALSE;

	// Set the bInheritHandle flag so pipe handles are inherited. 
	this->security_attrib.nLength = sizeof(SECURITY_ATTRIBUTES); 
    this->security_attrib.bInheritHandle = TRUE; 
    this->security_attrib.lpSecurityDescriptor = NULL; 

	// STDOUT/ERR redirection http://support.microsoft.com/kb/q190351/
	//
	// other references:
	// http://www.codeproject.com/KB/threads/redir.aspx?display=Print
	// http://www.codeproject.com/KB/threads/consolepipe.aspx
	// http://www.codeproject.com/KB/threads/redir.aspx
	// http://support.microsoft.com/kb/q105305/
	//
	// Create the child output pipe.
	// 
	if (! CreatePipe(&this->childStd_OUT_tmp, &this->childStd_OUT_Write, &this->security_attrib, 0)) 
	{
        this->logEvent(TEXT("Stdout Create Pipe failed!"), S_ERROR); 
	}

    // Create a duplicate of the output write handle for the std error
    // write handle. This is necessary in case the child application
    // closes one of its std output handles.
    if (!DuplicateHandle(
		GetCurrentProcess(),
		this->childStd_OUT_Write,
        GetCurrentProcess(),
		&this->childStd_ERR_Write,
		0,
		TRUE,DUPLICATE_SAME_ACCESS)
	) 
	{
        this->logEvent("Service::startProcess - DuplicateHandle stdout for stderr handler failed!", S_ERROR);
	}

    // Create new output read handle and the input write handles. Set
    // the Properties to FALSE. Otherwise, the child inherits the
    // properties and, as a result, non-closeable handles to the pipes
    // are created.
    if (!DuplicateHandle(
		GetCurrentProcess(),
		this->childStd_OUT_tmp,
        GetCurrentProcess(),
        &this->childStd_OUT_Read, // Address of new handle.
        0,
		FALSE, // Make it uninheritable.
        DUPLICATE_SAME_ACCESS)
	)
	{
        this->logEvent("Service::startProcess - DuplicateHandle failed!", S_ERROR);
	}

	// Close inheritable copies of the handles you do not want to be
    // inherited.
	if (!CloseHandle(this->childStd_OUT_tmp)) {
		this->logEvent("Service::startProcess - close childStd_OUT_tmp failed!", S_ERROR);
	}

	STARTUPINFO si;
	if (this->process_info == NULL) {
		this->process_info = new PROCESS_INFORMATION;
	}
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory(this->process_info, sizeof(PROCESS_INFORMATION));

	// Enable desktop interaction dependant on what the sets in the config file:
	if (this->has_gui == "yes") 
	{
		// SW_SHOWNORAL ref: http://msdn.microsoft.com/en-us/library/ms633548(VS.85).aspx
		si.wShowWindow = SW_SHOWNORMAL;
		si.lpDesktop = NULL; 
		si.dwFlags |= STARTF_USESHOWWINDOW;
		this->logEvent("Service::startProcess - setting up desktop interaction.", S_INFO);
	}

	// Disable stdout for the moment
    //si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    //si.hStdError = this->childStd_ERR_Write;
    //.hStdOutput = this->childStd_OUT_Write;
    //si.dwFlags |= STARTF_USESTDHANDLES;

    // Start the child process. 
	char pTemp[NAME_PATH_MAX_LENGTH + 255] = "";

	if(CreateProcess(
		NULL,           // No module name (use command line)
        (LPSTR) (process_name),    // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        TRUE,          // Set handle inheritance
        //CREATE_NEW_CONSOLE,             
		0, 
        NULL,           // Use parent's environment block
		(LPSTR) (working_path),    // Where (filesystem directory) to run the command from.
        &si,            // Pointer to STARTUPINFO structure
        this->process_info     // Pointer to PROCESS_INFORMATION structure
    )) 
    {
		sprintf(pTemp,"Service::startProcess: '%s' OK.\n", process_name); 
		this->logEvent(pTemp, S_INFO);
        rc = TRUE;

		job_assign = AssignProcessToJobObject(this->job_processes, this->process_info->hProcess);
		if(!(job_assign))
		{
			sprintf(pTemp,"Service::startProcess: error adding the new running process to our job.\n"); 
			this->logEvent(pTemp, S_ERROR);
		}
    }
	else
	{
		long err_code = getLastError();
		sprintf(pTemp,"Service::startProcess: '%s' FAIL. Error code '%d'.\n", process_name, err_code); 
	    this->logEvent(pTemp, S_ERROR);
	}

	// Close pipe handles (do not continue to modify the parent).
    // You need to make sure that no handles to the write end of the
    // output pipe are maintained in this process or else the pipe will
    // not close when the child process exits and the ReadFile will hang.
	if (!CloseHandle(this->childStd_OUT_Write)) {
		this->logEvent("Service::startProcess - close childStd_OUT_Write failed!", S_ERROR);
	}
	if (!CloseHandle(this->childStd_ERR_Write)) {
		this->logEvent("Service::startProcess - close childStd_ERR_Write failed!", S_ERROR);
	}

	return rc;
}

void Service::stopProcess()
{
	if (this->childStd_OUT_Read) {
		CloseHandle(this->childStd_OUT_Read);
		this->childStd_OUT_Read = NULL;
	}
	if (this->childStd_OUT_Write) {
		CloseHandle(this->childStd_OUT_Write);
		this->childStd_OUT_Write = NULL;
	}
	if (this->childStd_ERR_Read) {
		CloseHandle(this->childStd_ERR_Read);
		this->childStd_ERR_Read = NULL;
	}
	if (this->childStd_ERR_Write) {
		CloseHandle(this->childStd_ERR_Write);
		this->childStd_ERR_Write = NULL;
	}

	if(this->process_info)
	{
		// Post a WM_QUIT message first, attempting to politely ask it to exit:
		PostThreadMessage(this->process_info->dwThreadId, WM_QUIT, 0, 0);
		Sleep(4000);

		// Terminate the process by force
		//TerminateProcess(this->process_info->hProcess, 0);

		// Shutdown all running processes inside our job:
		//
		TerminateJobObject(this->job_processes, 0);
	}
}


// Store the configuration file and path in a registry,
// based on the service exe path. The exe should not be moved
// once it has been set up!
//
void Service::installAid(char *exe_path)
{
	long err_code = 0;
	char reg_start[] = "SOFTWARE\\StationService\\Services\\";
	int reg_start_length = strlen(reg_start);
	char reg_end[] = "\\setup";
	int reg_end_length = strlen(reg_start);
	int exe_length = strlen(exe_path);
	int max_exe_length = REG_PATH_MAX_LENGTH - (reg_start_length+reg_end_length);

	// Before setting up the registry key check we can fit in the 
	// space for it:
	//
	if (exe_length >= max_exe_length) 
	{
		char pTemp[NAME_PATH_MAX_LENGTH + 255] = "";
		sprintf(pTemp,"Service::installAid: exe path '%s' is too big. It must be >= %d.\n", exe_path, max_exe_length); 
	    this->logEvent(pTemp, S_ERROR);

		return;
	}

	// Ok, it'll fit:
	//
	strcpy(registry_path, reg_start);	
	strcat(registry_path, exe_path);							
	strcat(registry_path, reg_end);								

	HKEY service_key;
	DWORD rc = 0;
	DWORD disposition = 0;
	 
	rc = RegCreateKeyEx(
		HKEY_LOCAL_MACHINE, 
		registry_path, 
		0, 
		NULL,
		REG_OPTION_NON_VOLATILE, 
		KEY_WRITE, 
		NULL, 
		&service_key, 
		&disposition		
	);

	if (rc == ERROR_SUCCESS) 
	{
	    // Set the config file this service must use when it starts up:
		//
		rc = RegSetValueEx(
			  service_key,             // subkey handle 
			  "config_file",          // value name 
			  0,                       // must be zero 
			  REG_EXPAND_SZ,           // value type 
			  (LPBYTE) config_file,        // pointer to value data 
			  (DWORD) ((strlen(config_file)+1)*sizeof(char)) // data size
	    );

	    if (rc != ERROR_SUCCESS)
	    {
			err_code = getLastError();
			char pTemp[REG_PATH_MAX_LENGTH + 1024] = "";
			sprintf(pTemp,"Service::installAid: Could not set 'config_file' in registry '%s'! Error '%d'.\n", 
				registry_path,
				err_code
			); 
			this->logEvent(pTemp, S_ERROR);
	    }
        RegCloseKey(service_key);
	}
	else
	{
		err_code = getLastError();
		char pTemp[REG_PATH_MAX_LENGTH + 255] = "";
		sprintf(pTemp,"Service::installAid: Unable to create/open '%s'! Error '%d'.\n", 
			registry_path,
			err_code
		); 
		this->logEvent(pTemp, S_ERROR);
	}
}


// Uninstall the registry config for this service instance.
//
//
void Service::uninstallAid(void)
{
}


// Write a chunk from the childs STDOUT/STDERR to the log file. This 
// method is called periodically from the run() method, it is not 
// really meant to be used outside of this method. If the log file is
// not set up then no action is taken.
//
void Service::readWriteOutErrFromPipe(void)
{ 
	int rc = 0;
    DWORD read, written, available; 
    CHAR buffer[BUFSIZE] = ""; 
    BOOL success = FALSE;

    COMMTIMEOUTS noblockingallowed;
    // millisecond timeout values:
	//
    noblockingallowed.ReadIntervalTimeout = 500;
    noblockingallowed.ReadTotalTimeoutMultiplier = 1;
    noblockingallowed.WriteTotalTimeoutMultiplier = 1;
    noblockingallowed.WriteTotalTimeoutConstant = 1;

	// Only log if the log_file handle is present.
	if (1) //(this->log_file)
	{
	   //  Read in some output...
	   if (this->childStd_OUT_Read) 
	   {
		   // Don't block waiting for input as this will prevent run() from 
		   // monitoring and working correctly. It will be stuck at this 
		   // point otherwise.
		   //
           this->logEvent("Service::readWriteOutErrFromPipe: checking for output.", S_INFO);

		   rc = SetCommTimeouts(this->childStd_OUT_Read, &noblockingallowed);
           //rc = PeekNamedPipe(this->childStd_OUT_Read, NULL, 0, NULL, &available, NULL);
		   if (rc) 
		   {
			   success = ReadFile(this->childStd_OUT_Read, buffer, min(BUFSIZE, available), &read, NULL);
			   if(!success || read == 0) {
   				   this->logEvent("Service::readWriteOutErrFromPipe: stdout closed?", S_INFO);
				   return; 
			   }
			   else
			   {
   				   this->logEvent("Service::readWriteOutErrFromPipe: HERE HERE 2.1.2", S_INFO);

				   char pTemp[BUFSIZE + 255] = "";
				   sprintf(pTemp,"Service::readWriteOutErrFromPipe: '%s'", buffer); 
				   this->logEvent(pTemp, S_INFO);

				   // ... and write this output to the log file if any was found.
				   //success = WriteFile(this->log_file, buffer, read, &written, NULL);
				   //if (!success) { 
				   //   return; 
				   //}
			   }			   
		   }
		   else
		   {
//               this->logEvent("Service::readWriteOutErrFromPipe: no stdout output.", S_INFO);
		   }
	   }
	}
} 
