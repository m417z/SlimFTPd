# SlimFTPd

SlimFTPd is a fully standards-compliant FTP server implementation with an advanced virtual file system. It is extremely small, but don't let its file size deceive you: SlimFTPd packs a lot of bang for the kilobyte. It is written in pure Win32 C++ and requires no messy installer. SlimFTPd is a fully multi-threaded application that runs as a system service on Windows 98/ME or Windows NT/2K/XP, and it comes with a tool to simplify its installation or uninstallation as a system service. Once the service is started, SlimFTPd runs quietly in the background. It reads its configuration from a config file in the same folder as the executable, and it outputs all activity to a log file in the same place. The virtual file system allows you to mount any local drive or path to any virtual path on the server. This allows you to have multiple local drives represented on the server's virtual file system or just different folders from the same drive. SlimFTPd allows you to set individual permissions for server paths. Open slimftpd.conf in your favorite text editor to set up SlimFTPd's configuration. The format of SlimFTPd's config file is similar to Apache Web Server's for those familiar with Apache.

SlimFTPd features:

* Standards-compliant FTP server implementation that works with all major FTP clients
* Fully multi-threaded 32-bit application that runs as a Windows system service on all Windows platforms
* Supports passive mode transfers and allows resume of failed transfers
* Small memory footprint; won't hog system resources
* Easy configuration of server options through configuration file
* All activity logged to file
* Support for binding to a specific interface in multihomed environments
* User definable timeouts
* No installation routine; won't take over your system
* Supports all standard FTP commands: ABOR, APPE, CDUP/XCUP, CWD/XCWD, DELE, HELP, LIST, MKD/XMKD, NOOP, PASS, PASV, PORT, PWD/XPWD, QUIT, REIN, RETR, RMD/XRMD, RNFR/RNTO, STAT, STOR, SYST, TYPE, USER
* Supports these extended FTP commands: MDTM, NLST, REST, SIZE
* Supports setting of file timestamps
* Conforms to [RFC 959](http://www.ietf.org/rfc/rfc0959.txt) and [RFC 1123](http://www.ietf.org/rfc/rfc1123.txt) standards 

# Changes from original SlimFTPd 3.181

* Dropped support for non-NT OSes
* Unicode support
* Minor bug fixes
