// Inspired by https://github.com/dhylands/Arduino-logging-library

#ifndef LOGGING_H
#define LOGGING_H
#include <inttypes.h>
#include <stdarg.h>
#include <Arduino.h>


#define LOG_LEVEL_NOOUTPUT 0 
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_DEBUG 3
#define LOG_LEVEL_VERBOSE 4

// default loglevel if nothing is set from user
#define LOGLEVEL LOG_LEVEL_DEBUG 


#define CR "\n"
#define LOGGING_VERSION 1

class Logging {
private:
    int _level;
    long _baud;
    Print* _printer;
public:
    /*! 
	 * default Constructor
	 */
    Logging()
      : _level(LOG_LEVEL_NOOUTPUT),
        _baud(0),
        _printer(NULL) {}
	
    /** 
	* Initializing, must be called as first.
	* \param level - logging levels <= this will be logged.
	* \param baud - baud rate to initialize the serial port to.
	* \return void
	*
	*/
	void Init(int level, long baud);
	
    /**
    * Initializing, must be called as first. Note that if you use
    * this variant of Init, you need to initialize the baud rate
    * yourself, if printer happens to be a serial port.
    * \param level - logging levels <= this will be logged.
    * \param printer - place that logging output will be sent to.
    * \return void
    *
    */
    void Init(int level, Print *printer);

    /**
	* Output an error message. Output message contains
	* ERROR: followed by original msg
	* Error messages are printed out, at every loglevel
	* except 0 ;-)
	* \param msg format string to output
	* \param ... any number of variables
	* \return void
	*/
    void Error(const char* msg, ...);
	
    /**
	* Output an info message. Output message contains
	* Info messages are printed out at l
	* loglevels >= LOG_LEVEL_INFOS
	*
	* \param msg format string to output
	* \param ... any number of variables
	* \return void
	*/

   void Info(const char* msg, ...);
	
    /**
	* Output an debug message. Output message contains
	* Debug messages are printed out at l
	* loglevels >= LOG_LEVEL_DEBUG
	*
	* \param msg format string to output
	* \param ... any number of variables
	* \return void
	*/

    void Debug(const char* msg, ...);
	
    /**
	* Output an verbose message. Output message contains
	* Debug messages are printed out at l
	* loglevels >= LOG_LEVEL_VERBOSE
	*
	* \param msg format string to output
	* \param ... any number of variables
	* \return void
	*/

    void Verbose(const char* msg, ...);

    
private:
    void print(const char *format, va_list args);
};

extern Logging Log;
#endif
