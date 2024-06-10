#ifndef __NTIFS_WIN32_H__
#define __NTIFS_WIN32_H__

#include <windows.h>

typedef DWORD NTSTATUS;

#ifndef NT_ERROR
# define NT_ERROR(status) ((((ULONG) (status)) >> 30) == 3)
#endif

#ifndef DEVICE_TYPE
# define DEVICE_TYPE DWORD
#endif

#ifndef STATUS_NOT_IMPLEMENTED
# define STATUS_NOT_IMPLEMENTED ((NTSTATUS) 0xC0000002L)
#endif

/* MinGW already has it, mingw-w64 does not. */
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
typedef struct _REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            ULONG Version;	// Currently version 3
            WCHAR StringList[1];	// Multistring (Consecutive strings each ending with a NULL)
            /* There are normally 4 strings here. Ex:
              Package ID:	L"Microsoft.WindowsTerminal_8wekyb3d8bbwe"
              Entry Point:	L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"
              Executable:	L"C:\Program Files\WindowsApps\Microsoft.WindowsTerminal_1.4.3243.0_x64__8wekyb3d8bbwe\wt.exe"
              Applic. Type:	l"0"	// Integer as ASCII. "0" = Desktop bridge application; Else sandboxed UWP application
            */
        } AppExecLinkReparseBuffer;
        struct {
            UCHAR  DataBuffer[1];
        } GenericReparseBuffer;
    } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, * PREPARSE_DATA_BUFFER;

typedef struct _REPARSE_READ_BUFFER {
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;
    UCHAR  DataBuffer[1];
} REPARSE_READ_BUFFER, * PREPARSE_READ_BUFFER;
#define REPARSE_READ_BUFFER_HEADER_SIZE (sizeof(REPARSE_READ_BUFFER) - sizeof(UCHAR))

typedef struct _REPARSE_SYMLINK_READ_BUFFER { // For tag IO_REPARSE_TAG_SYMLINK
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;
    WORD   SubstituteNameOffset;
    WORD   SubstituteNameLength;
    WORD   PrintNameOffset;
    WORD   PrintNameLength;
    ULONG  Flags;
    WCHAR  PathBuffer[1];
} SYMLINK_READ_BUFFER, * PSYMLINK_READ_BUFFER;
#define SYMLINK_READ_BUFFER_HEADER_SIZE (sizeof(SYMLINK_READ_BUFFER) - sizeof(WCHAR))

typedef struct _REPARSE_MOUNTPOINT_READ_BUFFER { // For tag IO_REPARSE_TAG_MOUNT_POINT, aka. junctions
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;
    WORD   SubstituteNameOffset;
    WORD   SubstituteNameLength;
    WORD   PrintNameOffset;
    WORD   PrintNameLength;
    WCHAR  PathBuffer[1];
} MOUNTPOINT_READ_BUFFER, * PMOUNTPOINT_READ_BUFFER;
#define MOUNTPOINT_READ_BUFFER_HEADER_SIZE (sizeof(MOUNTPOINT_READ_BUFFER) - sizeof(WCHAR))

typedef struct _REPARSE_MOUNTPOINT_WRITE_BUFFER {
    DWORD  ReparseTag;
    DWORD  ReparseDataLength;
    WORD   Reserved;
    WORD   ReparseTargetLength;
    WORD   ReparseTargetMaximumLength;
    WORD   Reserved1;
    WCHAR  ReparseTarget[1];
} MOUNTPOINT_WRITE_BUFFER, * PMOUNTPOINT_WRITE_BUFFER;
#define MOUNTPOINT_WRITE_BUFFER_HEADER_SIZE (sizeof(MOUNTPOINT_WRITE_BUFFER) - sizeof(WCHAR))

// Universal Windows Platform (UWP) Application Execution Links
// Ref: https://www.tiraniddo.dev/2019/09/overview-of-windows-execution-aliases.html
typedef struct _REPARSE_APPEXECLINK_READ_BUFFER { // For tag IO_REPARSE_TAG_APPEXECLINK
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;
    ULONG  Version;	// Currently version 3
    WCHAR  StringList[1];	// Multistring (Consecutive strings each ending with a NUL)
    /* There are normally 4 strings here. Ex:
      Package ID:	L"Microsoft.WindowsTerminal_8wekyb3d8bbwe"
      Entry Point:	L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"
      Executable:	L"C:\Program Files\WindowsApps\Microsoft.WindowsTerminal_1.4.3243.0_x64__8wekyb3d8bbwe\wt.exe"
      Applic. Type:	l"0"	// Integer as ASCII. "0" = Desktop bridge application; Else sandboxed UWP application
    */
} APPEXECLINK_READ_BUFFER, * PAPPEXECLINK_READ_BUFFER;

// Linux Sub-System (LXSS) Symbolic Links
typedef struct _REPARSE_LX_SYMLINK_BUFFER {
    DWORD  ReparseTag;
    WORD	 ReparseDataLength;
    WORD	 Reserved;
    DWORD  FileType; 	// Value is apparently always 2 for symlinks.
    char   PathBuffer[1];	// POSIX path of symlink. UTF-8. Not \0 terminated.
} LX_SYMLINK_READ_BUFFER, * PLX_SYMLINK_READ_BUFFER;
#endif

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    } DUMMYUNIONNAME;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, * PIO_STATUS_BLOCK;

typedef enum _FILE_INFORMATION_CLASS {
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation,
    FileBothDirectoryInformation,
    FileBasicInformation,
    FileStandardInformation,
    FileInternalInformation,
    FileEaInformation,
    FileAccessInformation,
    FileNameInformation,
    FileRenameInformation,
    FileLinkInformation,
    FileNamesInformation,
    FileDispositionInformation,
    FilePositionInformation,
    FileFullEaInformation,
    FileModeInformation,
    FileAlignmentInformation,
    FileAllInformation,
    FileAllocationInformation,
    FileEndOfFileInformation,
    FileAlternateNameInformation,
    FileStreamInformation,
    FilePipeInformation,
    FilePipeLocalInformation,
    FilePipeRemoteInformation,
    FileMailslotQueryInformation,
    FileMailslotSetInformation,
    FileCompressionInformation,
    FileObjectIdInformation,
    FileCompletionInformation,
    FileMoveClusterInformation,
    FileQuotaInformation,
    FileReparsePointInformation,
    FileNetworkOpenInformation,
    FileAttributeTagInformation,
    FileTrackingInformation,
    FileIdBothDirectoryInformation,
    FileIdFullDirectoryInformation,
    FileValidDataLengthInformation,
    FileShortNameInformation,
    FileIoCompletionNotificationInformation,
    FileIoStatusBlockRangeInformation,
    FileIoPriorityHintInformation,
    FileSfioReserveInformation,
    FileSfioVolumeInformation,
    FileHardLinkInformation,
    FileProcessIdsUsingFileInformation,
    FileNormalizedNameInformation,
    FileNetworkPhysicalNameInformation,
    FileIdGlobalTxDirectoryInformation,
    FileIsRemoteDeviceInformation,
    FileAttributeCacheInformation,
    FileNumaNodeInformation,
    FileStandardLinkInformation,
    FileRemoteProtocolInformation,
    FileMaximumInformation
} FILE_INFORMATION_CLASS, * PFILE_INFORMATION_CLASS;

typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} FILE_BASIC_INFORMATION, * PFILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG         NumberOfLinks;
    BOOLEAN       DeletePending;
    BOOLEAN       Directory;
} FILE_STANDARD_INFORMATION, * PFILE_STANDARD_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION {
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION, * PFILE_INTERNAL_INFORMATION;

typedef struct _FILE_EA_INFORMATION {
    ULONG EaSize;
} FILE_EA_INFORMATION, * PFILE_EA_INFORMATION;

typedef struct _FILE_ACCESS_INFORMATION {
    ACCESS_MASK AccessFlags;
} FILE_ACCESS_INFORMATION, * PFILE_ACCESS_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, * PFILE_POSITION_INFORMATION;

typedef struct _FILE_MODE_INFORMATION {
    ULONG Mode;
} FILE_MODE_INFORMATION, * PFILE_MODE_INFORMATION;

typedef struct _FILE_ALIGNMENT_INFORMATION {
    ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFORMATION, * PFILE_ALIGNMENT_INFORMATION;

typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, * PFILE_NAME_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
    LARGE_INTEGER  EndOfFile;
} FILE_END_OF_FILE_INFORMATION, * PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_ALL_INFORMATION {
    FILE_BASIC_INFORMATION     BasicInformation;
    FILE_STANDARD_INFORMATION  StandardInformation;
    FILE_INTERNAL_INFORMATION  InternalInformation;
    FILE_EA_INFORMATION        EaInformation;
    FILE_ACCESS_INFORMATION    AccessInformation;
    FILE_POSITION_INFORMATION  PositionInformation;
    FILE_MODE_INFORMATION      ModeInformation;
    FILE_ALIGNMENT_INFORMATION AlignmentInformation;
    FILE_NAME_INFORMATION      NameInformation;
} FILE_ALL_INFORMATION, * PFILE_ALL_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION, * PFILE_DISPOSITION_INFORMATION;

typedef struct _FILE_PIPE_LOCAL_INFORMATION {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} FILE_PIPE_LOCAL_INFORMATION, * PFILE_PIPE_LOCAL_INFORMATION;

#define FILE_SYNCHRONOUS_IO_ALERT               0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT            0x00000020

typedef enum _FS_INFORMATION_CLASS {
    FileFsVolumeInformation = 1,
    FileFsLabelInformation = 2,
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsControlInformation = 6,
    FileFsFullSizeInformation = 7,
    FileFsObjectIdInformation = 8,
    FileFsDriverPathInformation = 9,
    FileFsVolumeFlagsInformation = 10,
    FileFsSectorSizeInformation = 11
} FS_INFORMATION_CLASS, * PFS_INFORMATION_CLASS;

typedef struct _FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG         VolumeSerialNumber;
    ULONG         VolumeLabelLength;
    BOOLEAN       SupportsObjects;
    WCHAR         VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION, * PFILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_FS_LABEL_INFORMATION {
    ULONG VolumeLabelLength;
    WCHAR VolumeLabel[1];
} FILE_FS_LABEL_INFORMATION, * PFILE_FS_LABEL_INFORMATION;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG         SectorsPerAllocationUnit;
    ULONG         BytesPerSector;
} FILE_FS_SIZE_INFORMATION, * PFILE_FS_SIZE_INFORMATION;

typedef struct _FILE_FS_DEVICE_INFORMATION {
    DEVICE_TYPE DeviceType;
    ULONG       Characteristics;
} FILE_FS_DEVICE_INFORMATION, * PFILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    LONG  MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, * PFILE_FS_ATTRIBUTE_INFORMATION;

typedef struct _FILE_FS_CONTROL_INFORMATION {
    LARGE_INTEGER FreeSpaceStartFiltering;
    LARGE_INTEGER FreeSpaceThreshold;
    LARGE_INTEGER FreeSpaceStopFiltering;
    LARGE_INTEGER DefaultQuotaThreshold;
    LARGE_INTEGER DefaultQuotaLimit;
    ULONG         FileSystemControlFlags;
} FILE_FS_CONTROL_INFORMATION, * PFILE_FS_CONTROL_INFORMATION;

typedef struct _FILE_FS_FULL_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG         SectorsPerAllocationUnit;
    ULONG         BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION, * PFILE_FS_FULL_SIZE_INFORMATION;

typedef struct _FILE_FS_OBJECTID_INFORMATION {
    UCHAR ObjectId[16];
    UCHAR ExtendedInfo[48];
} FILE_FS_OBJECTID_INFORMATION, * PFILE_FS_OBJECTID_INFORMATION;

typedef struct _FILE_FS_DRIVER_PATH_INFORMATION {
    BOOLEAN DriverInPath;
    ULONG   DriverNameLength;
    WCHAR   DriverName[1];
} FILE_FS_DRIVER_PATH_INFORMATION, * PFILE_FS_DRIVER_PATH_INFORMATION;

typedef struct _FILE_FS_VOLUME_FLAGS_INFORMATION {
    ULONG Flags;
} FILE_FS_VOLUME_FLAGS_INFORMATION, * PFILE_FS_VOLUME_FLAGS_INFORMATION;

typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION {
    ULONG LogicalBytesPerSector;
    ULONG PhysicalBytesPerSectorForAtomicity;
    ULONG PhysicalBytesPerSectorForPerformance;
    ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG Flags;
    ULONG ByteOffsetForSectorAlignment;
    ULONG ByteOffsetForPartitionAlignment;
} FILE_FS_SECTOR_SIZE_INFORMATION, * PFILE_FS_SECTOR_SIZE_INFORMATION;

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, * PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

#ifndef SystemProcessorPerformanceInformation
# define SystemProcessorPerformanceInformation 8
#endif

#ifndef FILE_DEVICE_FILE_SYSTEM
# define FILE_DEVICE_FILE_SYSTEM 0x00000009
#endif

#ifndef FILE_DEVICE_NETWORK
# define FILE_DEVICE_NETWORK 0x00000012
#endif

#ifndef METHOD_BUFFERED
# define METHOD_BUFFERED 0
#endif

#ifndef METHOD_IN_DIRECT
# define METHOD_IN_DIRECT 1
#endif

#ifndef METHOD_OUT_DIRECT
# define METHOD_OUT_DIRECT 2
#endif

#ifndef METHOD_NEITHER
#define METHOD_NEITHER 3
#endif

#ifndef METHOD_DIRECT_TO_HARDWARE
# define METHOD_DIRECT_TO_HARDWARE METHOD_IN_DIRECT
#endif

#ifndef METHOD_DIRECT_FROM_HARDWARE
# define METHOD_DIRECT_FROM_HARDWARE METHOD_OUT_DIRECT
#endif

#ifndef FILE_ANY_ACCESS
# define FILE_ANY_ACCESS 0
#endif

#ifndef FILE_SPECIAL_ACCESS
# define FILE_SPECIAL_ACCESS (FILE_ANY_ACCESS)
#endif

#ifndef FILE_READ_ACCESS
# define FILE_READ_ACCESS 0x0001
#endif

#ifndef FILE_WRITE_ACCESS
# define FILE_WRITE_ACCESS 0x0002
#endif

#ifndef CTL_CODE
# define CTL_CODE(device_type, function, method, access)                      \
    (((device_type) << 16) | ((access) << 14) | ((function) << 2) | (method))
#endif

#ifndef FSCTL_SET_REPARSE_POINT
# define FSCTL_SET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM,            \
                                          41,                                 \
                                          METHOD_BUFFERED,                    \
                                          FILE_SPECIAL_ACCESS)
#endif

#ifndef FSCTL_GET_REPARSE_POINT
# define FSCTL_GET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM,            \
                                          42,                                 \
                                          METHOD_BUFFERED,                    \
                                          FILE_ANY_ACCESS)
#endif

#ifndef FSCTL_DELETE_REPARSE_POINT
# define FSCTL_DELETE_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM,         \
                                             43,                              \
                                             METHOD_BUFFERED,                 \
                                             FILE_SPECIAL_ACCESS)
#endif

#ifndef IO_REPARSE_TAG_SYMLINK
# define IO_REPARSE_TAG_SYMLINK (0xA000000CL)
#endif

typedef VOID(NTAPI* PIO_APC_ROUTINE)
(PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG Reserved);

typedef ULONG(NTAPI* sRtlNtStatusToDosError)
(NTSTATUS Status);

typedef NTSTATUS(NTAPI* sNtDeviceIoControlFile)
(HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG IoControlCode,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength);

typedef NTSTATUS(NTAPI* sNtQueryInformationFile)
(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass);

typedef NTSTATUS(NTAPI* sNtSetInformationFile)
(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass);

typedef NTSTATUS(NTAPI* sNtQueryVolumeInformationFile)
(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FsInformation,
    ULONG Length,
    FS_INFORMATION_CLASS FsInformationClass);

typedef NTSTATUS(NTAPI* sNtQuerySystemInformation)
(UINT SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

#endif //!__NTIFS_WIN32_H__
