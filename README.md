# Log-structured-filesystem
In this project I implement a **Minimal Log-Structured File System (LFS) in C**, designed to efficiently manage file storage by writing data sequentially to a log. LFS minimizes disk seek times and improves write performance, making it ideal for scenarios with heavy write operations.
You can learn more about LFS in this video: https://www.youtube.com/watch?v=KTCkW_6zz2k
## Current Features

The following operations are implemented:
* Lookup: Looks up the existence of a file or directory given the parent inode number and file name.
* Create: Creates a file or directory in the parent directory specified by pinum.

## Planned Features

Here are the features that are yet to be implemented:
* Read & Write: Read and write file contents.
* Delete: Remove files or directories from the system.
* Garbage Collection: Efficiently reclaim disk space from deleted or obsolete log entries.

  
