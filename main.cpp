#include "service.hpp"
#include "SimpleOpt.h"

BaseService *service = NULL;

// This will be set up so that windows calls it
// when you start the service. The first argument
// of argv[0] is the service name.
//
void WINAPI serviceMain(DWORD argc, LPTSTR *argv)
{
	if (service) 
	{
		service->Service(argc, argv);
	}
}

void WINAPI serviceControl(DWORD opcode)
{
	if (service) 
	{
	    service->Control(opcode);
	}
}

void ShowUsage() \
{
    _tprintf(_T("\
Usage: \
[-i] Install Service \n \
[-c] <absolute path to config.ini> \n \
[-u] Uninstall \n \
[-?] [--help]\n"));

}


DWORD main(int argc, char *argv[])
{
	// Command line argument setup
	enum { OPT_HELP, OPT_CFG, OPT_ADD, OPT_DEL };
	CSimpleOpt::SOption g_rgOptions[] = {
		// ID       TEXT                TYPE
		{ OPT_ADD,   _T("-i"),        SO_NONE }, // install service
		{ OPT_DEL,   _T("-u"),        SO_NONE }, // remove service
		{ OPT_CFG,   _T("-c"),        SO_REQ_SEP}, // config file to use when installing/removing 
		{ OPT_HELP,  _T("-?"),        SO_NONE }, // "-?"
		{ OPT_HELP,  _T("-h"),        SO_NONE }, // "-?"
		{ OPT_HELP,  _T("--help"),    SO_NONE }, // "--help"
		SO_END_OF_OPTIONS                        // END
	};

	std::string config_file = "config.ini";
	bool install_service = FALSE;
	bool remove_service = FALSE;

	CSimpleOpt args(argc, argv, g_rgOptions);

	while (args.Next()) 
	{
		if (args.LastError() == SO_SUCCESS) 
		{
			// handle option, for example...
			// * OptionId() gets the identifier (i.e. OPT_HOGE)
			// * OptionText() gets the option text (i.e. "-f")
			// * OptionArg() gets the option argument (i.e. "/tmp/file.o")
			if (args.OptionId() == OPT_HELP) 
			{
                ShowUsage();
                return 0;
            }
			if (args.OptionId() == OPT_ADD) 
			{	
				install_service = TRUE;
			}
			if (args.OptionId() == OPT_CFG) 
			{	
				config_file = args.OptionArg();
			}
			if (args.OptionId() == OPT_DEL) 
			{	
				remove_service = TRUE;
			}
		}
		else {
			// handle error (see the error codes - enum ESOError)
			_tprintf(_T("Invalid argument: %s\n"), args.OptionText());
            return 1;
		}
	}

	// Create the service instance and then decided based on 
	// the command line whether we should install/remove/etc
	// the service.
	//
	service = new Service(
		config_file,
		serviceMain, 
		serviceControl
	);
    
	if(install_service)
	{
		std::cout << "Install '" << service->GetServiceName() << "'." << std::endl;
        service->Install();
		std::cout << "Installed '" << service->GetServiceName() << "' ok." << std::endl;
	}
    else if(remove_service)
	{
		std::cout << "Uninstall '" << service->GetServiceName() << "'." << std::endl;
        service->UnInstall();
		std::cout << "Uninstalled '" << service->GetServiceName() << "' ok." << std::endl;
	}
	else
	{
		// Default action which windows services will fall through too.
		std::cout << "Starting '" << service->GetServiceName() << "'." << std::endl;
        service->Startup();
		std::cout << "Started '" << service->GetServiceName() << "' ok." << std::endl;
	}

    DWORD exitcode = service->GetExitCode();
    delete service;

	std::cout << "Exit code: '" << exitcode << "'." << std::endl;

	return exitcode;
}
