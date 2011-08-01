ServiceStation
==============


Introduction
------------

ServiceStation allows you to run arbitrary programs as a service on the Windows
platform. The program you wish to run does not need to be changed to allow it to
work with ServiceStation or windows services.

This project was developed with an eye to running Python web services on
Windows, without the need to use and include Pywin32. This meant we could take
services running on Linux/Mac and run them unmodified on Windows.

  * http://sourceforge.net/apps/trac/servicestation/wiki


Download
--------

  * http://sourceforge.net/apps/trac/servicestation/wiki/Downloads


Quick Start
-----------

Download the latest version.

Decompress the latest download into a directory like "c:\servicestation".

Start cmd.exe and change into the "c:\servicestation" directory.

Then do:
<pre>
    servicestation.exe -i -c c:\servicestation\config.cfg
</pre>

A service called "A1Notepad" should now be installed in services. You can run it
from the Services GUI or using "net start A1Notepad".

Start the service and then look at task manager or ProcessExplorer if you have
it. You should see A1Notepad and then notepad.exe running in the background.

To remove the service you can do:

<pre>
    servicestation.exe -r -c c:\servicestation\config.cfg
</pre>

or

<pre>
    sc delete A1Notepad
</pre>

You should be able to edit the config.ini and change the command line after the
service is installed and it will start any other app.


Features
--------

  * Tracks all child processes launched by the command it runs and closes them
    on stop/restart.
  * Monitors the command its running and keeps it alive.
  * Allows you to set the description / name from the configuration file.
  * It logs useful information to the event viewer so you can see why it
    couldn't run the command under its care.
  * Can interact with the desktop or not so you can run programs with a GUI but
    hide it.
  * Does not disconnect the service if you log-out as some service runners do.
  * Runs any command line as a service (see notepad example).
  * Small download < 200K.
  * Easy to use and include with projects.
  * A flexible license allowing inclusion in commercial projects.
  * Works on all Windows platforms starting with 2000.
