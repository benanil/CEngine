/*
 * Dirent interface for Microsoft Visual Studio
 *
 * Copyright (C) 1998-2019 Toni Ronkko
 * This file is part of dirent.  Dirent may be rpfreely distributed
 * under the MIT license.  For all details and documentation, see
 * https://github.com/tronkko/dirent
 */
#ifndef DIRENT_H
#define DIRENT_H

/* Hide warnings about unreferenced local functions */
#if defined(__clang__)
#	pragma clang diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#	pragma warning(disable:4505)
#elif defined(__GNUC__)
#	pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include <stdarg.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define _DIRENT_HAVE_D_TYPE

#define _DIRENT_HAVE_D_NAMLEN

#if !defined(FILE_ATTRIBUTE_DEVICE)
#	define FILE_ATTRIBUTE_DEVICE 0x40
#endif

#if !defined(S_IFMT)
#	define S_IFMT _S_IFMT
#endif

#if !defined(S_IFDIR)
#	define S_IFDIR _S_IFDIR
#endif

#if !defined(S_IFCHR)
#	define S_IFCHR _S_IFCHR
#endif

#if !defined(S_IFFIFO)
#	define S_IFFIFO _S_IFFIFO
#endif

#if !defined(S_IFREG)
#	define S_IFREG _S_IFREG
#endif

#if !defined(S_IREAD)
#	define S_IREAD _S_IREAD
#endif

#if !defined(S_IWRITE)
#	define S_IWRITE _S_IWRITE
#endif

#if !defined(S_IEXEC)
#	define S_IEXEC _S_IEXEC
#endif

#if !defined(S_IFIFO)
#	define S_IFIFO _S_IFIFO
#endif

#if !defined(S_IFBLK)
#	define S_IFBLK 0
#endif

#if !defined(S_IFLNK)
#	define S_IFLNK (_S_IFDIR | _S_IFREG)
#endif

#if !defined(S_IFSOCK)
#	define S_IFSOCK 0
#endif

#if !defined(S_IRUSR)
#	define S_IRUSR S_IREAD
#endif

#if !defined(S_IWUSR)
#	define S_IWUSR S_IWRITE
#endif

#if !defined(S_IXUSR)
#	define S_IXUSR 0
#endif

#if !defined(S_IRWXU)
#	define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif

#if !defined(S_IRGRP)
#	define S_IRGRP 0
#endif

#if !defined(S_IWGRP)
#	define S_IWGRP 0
#endif

#if !defined(S_IXGRP)
#	define S_IXGRP 0
#endif

#if !defined(S_IRWXG)
#	define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#endif

#if !defined(S_IROTH)
#	define S_IROTH 0
#endif

#if !defined(S_IWOTH)
#	define S_IWOTH 0
#endif

#if !defined(S_IXOTH)
#	define S_IXOTH 0
#endif

#if !defined(S_IRWXO)
#	define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#endif

#if !defined(PATH_MAX)
#	define PATH_MAX MAX_PATH
#endif
#if !defined(FILENAME_MAX)
#	define FILENAME_MAX MAX_PATH
#endif
#if !defined(NAME_MAX)
#	define NAME_MAX FILENAME_MAX
#endif

#define DT_UNKNOWN 0
#define DT_REG S_IFREG
#define DT_DIR S_IFDIR
#define DT_FIFO S_IFIFO
#define DT_SOCK S_IFSOCK
#define DT_CHR S_IFCHR
#define DT_BLK S_IFBLK
#define DT_LNK S_IFLNK

#define IFTODT(mode) ((mode) & S_IFMT)
#define DTTOIF(type) (type)

#if !defined(S_ISFIFO)
#	define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#endif
#if !defined(S_ISDIR)
#	define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif
#if !defined(S_ISREG)
#	define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif
#if !defined(S_ISLNK)
#	define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#endif
#if !defined(S_ISSOCK)
#	define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#endif
#if !defined(S_ISCHR)
#	define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#endif
#if !defined(S_ISBLK)
#	define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)
#endif

#define _D_EXACT_NAMLEN(p) ((p)->d_namlen)

#define _D_ALLOC_NAMLEN(p) ((PATH_MAX)+1)


#ifdef __cplusplus
extern "C" {
#endif


struct _wdirent {
	long d_ino;
	long d_off;
	unsigned short d_reclen;
	size_t d_namlen;
	int d_type;
	wchar_t d_name[PATH_MAX+1];
};
typedef struct _wdirent _wdirent;

struct _WDIR {
	struct _wdirent ent;
	WIN32_FIND_DATAW data;
	int cached;
	int invalid;
	HANDLE handle;
	wchar_t *patt;
};
typedef struct _WDIR _WDIR;

struct dirent {
	long d_ino;
	long d_off;
	unsigned short d_reclen;
	size_t d_namlen;
	int d_type;
	char d_name[PATH_MAX+1];
};
typedef struct dirent dirent;

struct DIR {
	struct dirent ent;
	struct _WDIR *wdirp;
};
typedef struct DIR DIR;


static DIR *opendir(const char *dirname);
static _WDIR *_wopendir(const wchar_t *dirname);

static struct dirent *readdir(DIR *dirp);
static struct _wdirent *_wreaddir(_WDIR *dirp);

static int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
static int _wreaddir_r(_WDIR *dirp, struct _wdirent *entry, struct _wdirent **result);

static int closedir(DIR *dirp);
static int _wclosedir(_WDIR *dirp);

/* For compatibility with Symbian */
#define wdirent _wdirent
#define WDIR _WDIR
#define wopendir _wopendir
#define wreaddir _wreaddir
#define wclosedir _wclosedir

/* Compatibility with older Microsoft compilers and non-Microsoft compilers */
#if !defined(_MSC_VER) || _MSC_VER < 1400
#	define wcstombs_s dirent_wcstombs_s
#	define mbstowcs_s dirent_mbstowcs_s
#endif

/* Optimize dirent_set_errno() away on modern Microsoft compilers */
#if defined(_MSC_VER) && _MSC_VER >= 1400
#	define dirent_set_errno _set_errno
#endif

static WIN32_FIND_DATAW *dirent_first(_WDIR *dirp);
static WIN32_FIND_DATAW *dirent_next(_WDIR *dirp);
static long dirent_hash(WIN32_FIND_DATAW *datap);

#if !defined(_MSC_VER) || _MSC_VER < 1400
static int dirent_mbstowcs_s(
	size_t *pReturnValue, wchar_t *wcstr, size_t sizeInWords,
	const char *mbstr, size_t count);
#endif

#if !defined(_MSC_VER) || _MSC_VER < 1400
static int dirent_wcstombs_s(
	size_t *pReturnValue, char *mbstr, size_t sizeInBytes,
	const wchar_t *wcstr, size_t count);
#endif

#if !defined(_MSC_VER) || _MSC_VER < 1400
static void dirent_set_errno(int error);
#endif


static _WDIR * _wopendir(const wchar_t *dirname)
{
	wchar_t *p;
	if (dirname == NULL || dirname[0] == '\0') {
		dirent_set_errno(ENOENT);
		return NULL;
	}

	_WDIR *dirp = (_WDIR*) AllocateTLSFGlobal(sizeof(struct _WDIR));
	if (!dirp)
		return NULL;

	dirp->handle = INVALID_HANDLE_VALUE;
	dirp->patt = NULL;
	dirp->cached = 0;
	dirp->invalid = 0;

#if !defined(WINAPI_FAMILY_PARTITION) || WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	DWORD n = GetFullPathNameW(dirname, 0, NULL, NULL);
#else
	size_t n = wcslen(dirname);
#endif

	dirp->patt = (wchar_t*) AllocateTLSFGlobal(sizeof(wchar_t) * n + 16);
	if (dirp->patt == NULL)
		goto exit_closedir;

#if !defined(WINAPI_FAMILY_PARTITION) || WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	n = GetFullPathNameW(dirname, n, dirp->patt, NULL);
	if (n <= 0)
		goto exit_closedir;
#else
	wcsncpy_s(dirp->patt, n+1, dirname, n);
#endif

	p = dirp->patt + n;
	switch (p[-1]) {
	case '\\':
	case '/':
	case ':':
		break;

	default:
		*p++ = '\\';
	}
	*p++ = '*';
	*p = '\0';

	if (!dirent_first(dirp))
		goto exit_closedir;

	return dirp;

exit_closedir:
	_wclosedir(dirp);
	return NULL;
}

static struct _wdirent * _wreaddir(_WDIR *dirp)
{
	struct _wdirent *entry;
	(void) _wreaddir_r(dirp, &dirp->ent, &entry);
	return entry;
}

static int _wreaddir_r(
	_WDIR *dirp, struct _wdirent *entry, struct _wdirent **result)
{
	if (!dirp || dirp->handle == INVALID_HANDLE_VALUE || !dirp->patt) {
		dirent_set_errno(EBADF);
		*result = NULL;
		return -1;
	}

	WIN32_FIND_DATAW *datap = dirent_next(dirp);
	if (!datap) {
		*result = NULL;
		return /*OK*/0;
	}

	size_t i = 0;
	while (i < PATH_MAX && datap->cFileName[i] != 0) {
		entry->d_name[i] = datap->cFileName[i];
		i++;
	}
	entry->d_name[i] = 0;
	entry->d_namlen = i;

	DWORD attr = datap->dwFileAttributes;
	if ((attr & FILE_ATTRIBUTE_DEVICE) != 0)
		entry->d_type = DT_CHR;
	else if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		entry->d_type = DT_LNK;
	else if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
		entry->d_type = DT_DIR;
	else
		entry->d_type = DT_REG;

	datap = dirent_next(dirp);
	if (datap) {
		entry->d_off = dirent_hash(datap);

		dirp->cached = 1;
	} else {
		entry->d_off = (long) ((~0UL) >> 1);
	}

	entry->d_ino = 0;
	entry->d_reclen = sizeof(struct _wdirent);

	*result = entry;
	return /*OK*/0;
}

static int _wclosedir(_WDIR *dirp)
{
	if (!dirp) {
		dirent_set_errno(EBADF);
		return /*failure*/-1;
	}

	if (dirp->handle != INVALID_HANDLE_VALUE) {
		FindClose(dirp->handle);
	}

	DeAllocateTLSFGlobal(dirp->patt);
	DeAllocateTLSFGlobal(dirp);
	return /*success*/0;
}

static WIN32_FIND_DATAW * dirent_first(_WDIR *dirp)
{
	dirp->handle = FindFirstFileExW(
		dirp->patt, FindExInfoStandard, &dirp->data,
		FindExSearchNameMatch, NULL, 0);
	if (dirp->handle == INVALID_HANDLE_VALUE)
		goto error;

	dirp->cached = 1;
	return &dirp->data;

error:
	dirp->cached = 0;
	dirp->invalid = 1;

	DWORD errorcode = GetLastError();
	switch (errorcode) {
	case ERROR_ACCESS_DENIED:
		dirent_set_errno(EACCES);
		break;

	case ERROR_DIRECTORY:
		dirent_set_errno(ENOTDIR);
		break;

	case ERROR_PATH_NOT_FOUND:
	default:
		dirent_set_errno(ENOENT);
	}
	return NULL;
}

static WIN32_FIND_DATAW * dirent_next(_WDIR *dirp)
{
	if (dirp->invalid)
		return NULL;

	if (dirp->cached) {
		dirp->cached = 0;
		return &dirp->data;
	}

	if (FindNextFileW(dirp->handle, &dirp->data) == FALSE) {
		return NULL;
	}

	return &dirp->data;
}

static long dirent_hash(WIN32_FIND_DATAW *datap)
{
	unsigned long hash = 5381;
	unsigned long c;
	const wchar_t *p = datap->cFileName;
	const wchar_t *e = p + MAX_PATH;
	while (p != e && (c = *p++) != 0) {
		hash = (hash << 5) + hash + c;
	}

	return (long) (hash & ((~0UL) >> 1));
}

static DIR *opendir(const char *dirname)
{
	if (dirname == NULL || dirname[0] == '\0') {
		dirent_set_errno(ENOENT);
		return NULL;
	}

	struct DIR *dirp = (DIR*) AllocateTLSFGlobal(sizeof(struct DIR));
	if (!dirp)
		return NULL;

	wchar_t wname[PATH_MAX + 1];
	size_t n;
	int error = mbstowcs_s(&n, wname, PATH_MAX + 1, dirname, PATH_MAX+1);
	if (error)
		goto exit_failure;

	dirp->wdirp = _wopendir(wname);
	if (!dirp->wdirp)
		goto exit_failure;

	return dirp;

exit_failure:
	DeAllocateTLSFGlobal(dirp);
	return NULL;
}

static struct dirent * readdir(DIR *dirp)
{
	struct dirent *entry;
	(void) readdir_r(dirp, &dirp->ent, &entry);
	return entry;
}

static int readdir_r(
	DIR *dirp, struct dirent *entry, struct dirent **result)
{
	WIN32_FIND_DATAW *datap = dirent_next(dirp->wdirp);
	if (!datap) {
		*result = NULL;
		return /*OK*/0;
	}

	size_t n;
	int error = wcstombs_s(
		&n, entry->d_name, PATH_MAX + 1,
		datap->cFileName, PATH_MAX + 1);

	if (error && datap->cAlternateFileName[0] != '\0') {
		error = wcstombs_s(
			&n, entry->d_name, PATH_MAX + 1,
			datap->cAlternateFileName, PATH_MAX + 1);
	}

	if (!error) {
		entry->d_namlen = n - 1;

		DWORD attr = datap->dwFileAttributes;
		if ((attr & FILE_ATTRIBUTE_DEVICE) != 0)
			entry->d_type = DT_CHR;
		else if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			entry->d_type = DT_LNK;
		else if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
			entry->d_type = DT_DIR;
		else
			entry->d_type = DT_REG;

		datap = dirent_next(dirp->wdirp);
		if (datap) {
			entry->d_off = dirent_hash(datap);

			dirp->wdirp->cached = 1;
		} else {
			entry->d_off = (long) ((~0UL) >> 1);
		}

		entry->d_ino = 0;
		entry->d_reclen = sizeof(struct dirent);
	} else {
		entry->d_name[0] = '?';
		entry->d_name[1] = '\0';
		entry->d_namlen = 1;
		entry->d_type = DT_UNKNOWN;
		entry->d_ino = 0;
		entry->d_off = -1;
		entry->d_reclen = 0;
	}

	*result = entry;
	return /*OK*/0;
}

static int closedir(DIR *dirp)
{
	int ok;

	if (!dirp)
		goto exit_failure;

	ok = _wclosedir(dirp->wdirp);
	dirp->wdirp = NULL;

	DeAllocateTLSFGlobal(dirp);
	return ok;

exit_failure:
	dirent_set_errno(EBADF);
	return /*failure*/-1;
}

#if !defined(_MSC_VER) || _MSC_VER < 1400
static int dirent_mbstowcs_s(
	size_t *pReturnValue, wchar_t *wcstr,
	size_t sizeInWords, const char *mbstr, size_t count)
{
	size_t n = mbstowcs(wcstr, mbstr, sizeInWords);
	if (wcstr && n >= count)
		return /*error*/ 1;

	if (wcstr && sizeInWords) {
		if (n >= sizeInWords)
			n = sizeInWords - 1;
		wcstr[n] = 0;
	}

	if (pReturnValue) 
		*pReturnValue = n + 1;

	return 0;
}
#endif

#if !defined(_MSC_VER) || _MSC_VER < 1400
static int dirent_wcstombs_s( size_t *pReturnValue, char *mbstr, size_t sizeInBytes, const wchar_t *wcstr, size_t count)
{
	size_t n = wcstombs(mbstr, wcstr, sizeInBytes);
	if (mbstr && n >= count)
		return /*error*/1;

	if (mbstr && sizeInBytes) {
		if (n >= sizeInBytes) {
			n = sizeInBytes - 1;
		}
		mbstr[n] = '\0';
	}

	if (pReturnValue) {
		*pReturnValue = n + 1;
	}
	return 0;
}
#endif

#if !defined(_MSC_VER) || _MSC_VER < 1400
static void
dirent_set_errno(int error)
{
	errno = error;
}
#endif

#ifdef __cplusplus
}
#endif
#endif /*DIRENT_H*/
