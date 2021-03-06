/*

See License.txt to see what this project is licensed under.

Oisin Mulvihill
2009-04-20

*/
#ifndef _the_service_h_
#define _the_service_h_

#include "servicebase.hpp"

#define NAME_PATH_MAX_LENGTH 2048
#define REG_PATH_MAX_LENGTH 2048
#define SERVICE_DESC_MAX_LENGTH 256


#define BUFSIZE 4096 
 
// logEvent: levels
//
#define S_INFO 1
#define S_WARN 2
#define S_ERROR 3

class Service : public ServiceBase
{
    HANDLE childStd_ERR_Read;
	HANDLE childStd_ERR_Write;
	HANDLE childStd_OUT_Read;
	HANDLE childStd_OUT_Write;
	HANDLE childStd_OUT_tmp;
	HANDLE log_file;
	SECURITY_ATTRIBUTES security_attrib; 

	// All processes we start will be associated with this
	// so they can be killed if we are.
	HANDLE job_processes;

	//
	boolean is_running;

	// Contains yes or no to indicate whether the service interacts with the desktop:
	std::string has_gui;

	// Where this instances configuration is stored in the registry
	char registry_path[REG_PATH_MAX_LENGTH];

	// The absolute path and file of the windows style configuration ini file:
	char config_file[NAME_PATH_MAX_LENGTH]; 

	// What and were to run:
	char process_name[NAME_PATH_MAX_LENGTH]; 

	// Where to log the child's STDOUT/ERR to:
	char log_file_name[MAX_PATH]; 

	// Where to run the command from:
	char working_path[NAME_PATH_MAX_LENGTH];

	PROCESS_INFORMATION	*process_info;

private:
    Service(void);
    Service(Service&);
    
protected:
    
	// Called when the service starts up to set the service up based in registry indicated config file.
	DWORD init(DWORD argc, LPTSTR* argv);

	// Called directly after a successfull call to init(). Start the child
	// process and monitor it, restarting as needed. Log the stdout/err to
	// file.
	//
    int run();

	// Set a note about what this service does:
	bool setDescription(std::string description);

	// true: enable desktop interaction, false: disable interaction.
	bool interactiveState(bool interactive_state);

	// Called when its time to stop the service runing.
    void onStop(void);

	// Start the child process running.
	bool startProcess(void);

	// Stop the child process terminating it if needs be.
	void stopProcess(void);

	// Load the service insance configuration.
	int setupFromConfiguration(void);
	int setupFromConfiguration(const char *config_filename);

	// Log a message to the window event log.
	void logEvent(const char *message, int level);

	// Log some child process stdout/err to file:
	void readWriteOutErrFromPipe(void);

	// Add / Remove this instances registries settings.
	void installAid(char *exe_path);
	void uninstallAid(void);

public:
	Service(
		std::string config_file, 
		LPSERVICE_MAIN_FUNCTION  service_main, 
		LPHANDLER_FUNCTION service_control
	);

	~Service(void);
};

#endif
