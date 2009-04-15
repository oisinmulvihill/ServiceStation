#include "SimpleIni.h"

#include "service.hpp"


Service::Service(
		std::string config_file, 
		LPSERVICE_MAIN_FUNCTION  fpSrvMain, 
		LPHANDLER_FUNCTION fpSrvCtrl
	)
    : BaseService(fpSrvMain, fpSrvCtrl)
{
	int rc = 0;

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
	memset(&(this->config_file[0]), 0, NAME_PATH_MAX_LENGTH);
	strncpy(&(this->config_file[0]), config_file.c_str(), NAME_PATH_MAX_LENGTH);

	rc = this->SetupFromConfiguration(this->config_file);
	if (rc != NO_ERROR) 
	{
		// This means we are probably running in service mode. The Init() 
		// call will attempt to use the registry to recover and setup the 
		// service. If this fails the service will be stopped correctly,
		// which we can't do at this stage.
		//
		std::cout << "Possible error loading '" << config_file << "'. Init() will handle this." << std::endl;
		std::cout << "Using default values for the moment." << std::endl;
		//return;
	}

	// What we have configured / defaults set up:
	//
	std::cout << "1. config_file '" << (const char *) this->config_file << "'." << std::endl;
	std::cout << "2. service_name '" << this->GetServiceName() << "'." << std::endl;
	std::cout << "3. working_dir '" << (const char *) this->working_path << "'." << std::endl;
	std::cout << "4. log_file '" << (const char *) this->log_file_name << "'." << std::endl;


	// Used by StartProcess...
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
void Service::LogEvent(const char *message, int level) 
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

	HANDLE event_source = RegisterEventSource(NULL, this->GetServiceName());      
	if(event_source == NULL) {
		std::cerr << "Unable to register source: '" << this->GetServiceName() << "'." << std::endl;     
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
// or via the registry. The construct or Init() will call this
// method. The file will return NO_ERROR if everything is ok.
//
int Service::SetupFromConfiguration(const char *config_filename)
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
		this->LogEvent(pTemp, S_ERROR);
		return 1;
	}

	// Create the process job which we'll use to contain our processes in:
	// ref: http://msdn.microsoft.com/en-us/library/ms684161.aspx
	//
	this->job_processes = CreateJobObject(NULL, "servicestation-jobs");
	if (!(this->job_processes))
	{
		this->LogEvent("Error creating service station job container!", S_ERROR);
		return 1;
	}

	// Set up the name of this service:
	//
	std::string service_name = ini.GetValue("service", "name", "ServiceRunner");
	this->SetServiceName(service_name);

	// Set up the command which is to be run as a service:
	//
	std::string command_line = ini.GetValue("service", "command_line", "cmd.exe");
	if (command_line.length() < 1)
	{
		this->LogEvent("Error command_line was an empty string!", S_ERROR);
		return 1;
	}
	memset(&(this->process_name[0]), 0, NAME_PATH_MAX_LENGTH);
	strncpy(&(this->process_name[0]), command_line.c_str(), NAME_PATH_MAX_LENGTH);
	
	// Set up where the process is run from:
	//
	std::string working_dir = ini.GetValue("service", "working_dir", "c:\\");
	memset(&(this->working_path[0]), 0, NAME_PATH_MAX_LENGTH);
	strncpy(&(this->working_path[0]), working_dir.c_str(), NAME_PATH_MAX_LENGTH);

	// The file to write the child processes STDOUT/ERR to:
	//
	std::string the_log_file = ini.GetValue("service", "log_file", "child_out_err.log");
	memset(&(this->log_file_name[0]), 0, MAX_PATH);
	strncpy(&(this->log_file_name[0]), the_log_file.c_str(), MAX_PATH);

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
 //     this->LogEvent(pTemp, S_ERROR);
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
DWORD Service::Init(DWORD ac, LPTSTR *av)
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
		sprintf(pTemp, "Service::Init(): Cannot recover the registry as the exe path and name are too large!");
		this->LogEvent(pTemp, S_ERROR);
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
			this->LogEvent("Service::Init(): Could get the config file value from registry!", S_INFO);		
			return 1;
		}
		else
		{
			rc = this->SetupFromConfiguration(data);
		}
        RegCloseKey(service_key);
	}
	else
	{
		char pTemp[REG_PATH_MAX_LENGTH + 255] = "";
		sprintf(pTemp, "Service::Init(): Unable to open registry key '%s'!", registry_path);
		this->LogEvent(pTemp, S_ERROR);
		return 1;
	}

    return rc;
}

int Service::Run( void )
{
	this->is_running = true;
	this->StartProcess();

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
				if(this->StartProcess())
				{
					this->LogEvent("Run: Restarted process ok.\n", S_WARN);
				}
			}
		}
		else 
		{
			long nError = GetLastError();
			char pTemp[1024];
			sprintf(pTemp,"Service::Run: unable to start service! Error code = %d\n", nError); 
			this->LogEvent(pTemp, S_ERROR);
		}

		// Log any child process console activity to the log file:
		//
		//this->ReadWriteOutErrFromPipe();

		// Wait a bit so we're not hogging CPU time too much.
		Sleep(1000);
	}
    
	// Ok, time to exit tell out child process to stop as well.
	this->EndProcess();

	return NO_ERROR; 
}


// Called by windows to stop the service running.
//
void Service::OnStop( void )
{
	this->LogEvent("Service::OnStop - Exit time", S_INFO);
	this->is_running = false;
	this->EndProcess();
}


// Make an attempt to start the child process. If this fails we'll
// be called again by Run() when it detects the child process is
// no longer running.
//
bool Service::StartProcess()
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
        this->LogEvent(TEXT("Stdout Create Pipe failed!"), S_ERROR); 
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
        this->LogEvent("Service::StartProcess - DuplicateHandle stdout for stderr handler failed!", S_ERROR);
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
        this->LogEvent("Service::StartProcess - DuplicateHandle failed!", S_ERROR);
	}

	// Close inheritable copies of the handles you do not want to be
    // inherited.
	if (!CloseHandle(this->childStd_OUT_tmp)) {
		this->LogEvent("Service::StartProcess - close childStd_OUT_tmp failed!", S_ERROR);
	}

	STARTUPINFO si;
	if (this->process_info == NULL) {
		this->process_info = new PROCESS_INFORMATION;
	}
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory(this->process_info, sizeof(PROCESS_INFORMATION));

	// No GUI interaction. Could this be a config file option?
	//
	//si.wShowWindow = SW_SHOW;
    //si.dwFlags |= STARTF_USESHOWWINDOW;
	// We could change this to the desktop we should interact with:
	//si.lpDesktop = NULL; 
	//
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
		sprintf(pTemp,"Service::StartProcess: '%s' OK.\n", process_name); 
		this->LogEvent(pTemp, S_INFO);
        rc = TRUE;

		job_assign = AssignProcessToJobObject(this->job_processes, this->process_info->hProcess);
		if(!(job_assign))
		{
			sprintf(pTemp,"Service::StartProcess: error adding the new running process to our job.\n"); 
			this->LogEvent(pTemp, S_ERROR);
		}
    }
	else
	{
		long err_code = GetLastError();
		sprintf(pTemp,"Service::StartProcess: '%s' FAIL. Error code '%d'.\n", process_name, err_code); 
	    this->LogEvent(pTemp, S_ERROR);
	}

	// Close pipe handles (do not continue to modify the parent).
    // You need to make sure that no handles to the write end of the
    // output pipe are maintained in this process or else the pipe will
    // not close when the child process exits and the ReadFile will hang.
	if (!CloseHandle(this->childStd_OUT_Write)) {
		this->LogEvent("Service::StartProcess - close childStd_OUT_Write failed!", S_ERROR);
	}
	if (!CloseHandle(this->childStd_ERR_Write)) {
		this->LogEvent("Service::StartProcess - close childStd_ERR_Write failed!", S_ERROR);
	}

	return rc;
}

void Service::EndProcess()
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
void Service::InstallAid(char *exe_path)
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
		sprintf(pTemp,"Service::InstallAid: exe path '%s' is too big. It must be >= %d.\n", exe_path, max_exe_length); 
	    this->LogEvent(pTemp, S_ERROR);

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
			err_code = GetLastError();
			char pTemp[REG_PATH_MAX_LENGTH + 1024] = "";
			sprintf(pTemp,"Service::InstallAid: Could not set 'config_file' in registry '%s'! Error '%d'.\n", 
				registry_path,
				err_code
			); 
			this->LogEvent(pTemp, S_ERROR);
	    }
        RegCloseKey(service_key);
	}
	else
	{
		err_code = GetLastError();
		char pTemp[REG_PATH_MAX_LENGTH + 255] = "";
		sprintf(pTemp,"Service::InstallAid: Unable to create/open '%s'! Error '%d'.\n", 
			registry_path,
			err_code
		); 
		this->LogEvent(pTemp, S_ERROR);
	}
}


// Uninstall the registry config for this service instance.
//
//
void Service::UnInstallAid(void)
{
}


// Write a chunk from the childs STDOUT/STDERR to the log file. This 
// method is called periodically from the Run() method, it is not 
// really meant to be used outside of this method. If the log file is
// not set up then no action is taken.
//
void Service::ReadWriteOutErrFromPipe(void)
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
		   // Don't block waiting for input as this will prevent Run() from 
		   // monitoring and working correctly. It will be stuck at this 
		   // point otherwise.
		   //
           this->LogEvent("Service::ReadWriteOutErrFromPipe: checking for output.", S_INFO);

		   rc = SetCommTimeouts(this->childStd_OUT_Read, &noblockingallowed);
           //rc = PeekNamedPipe(this->childStd_OUT_Read, NULL, 0, NULL, &available, NULL);
		   if (rc) 
		   {
			   success = ReadFile(this->childStd_OUT_Read, buffer, min(BUFSIZE, available), &read, NULL);
			   if(!success || read == 0) {
   				   this->LogEvent("Service::ReadWriteOutErrFromPipe: stdout closed?", S_INFO);
				   return; 
			   }
			   else
			   {
   				   this->LogEvent("Service::ReadWriteOutErrFromPipe: HERE HERE 2.1.2", S_INFO);

				   char pTemp[BUFSIZE + 255] = "";
				   sprintf(pTemp,"Service::ReadWriteOutErrFromPipe: '%s'", buffer); 
				   this->LogEvent(pTemp, S_INFO);

				   // ... and write this output to the log file if any was found.
				   //success = WriteFile(this->log_file, buffer, read, &written, NULL);
				   //if (!success) { 
				   //   return; 
				   //}
			   }			   
		   }
		   else
		   {
//               this->LogEvent("Service::ReadWriteOutErrFromPipe: no stdout output.", S_INFO);
		   }
	   }
	}
} 
