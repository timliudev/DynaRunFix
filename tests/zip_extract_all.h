// zip_extract_all: extracts every entry (stored or deflate, not encrypted) of a zip below dest (which must exist); an unsafe name, a damaged entry or a zip
// larger than the limits fails the whole call (PK_FORMAT). *nfiles: files written. Nothing in the shipped code unpacks a whole zip (the driver and the
// DynaRun setup unpack only the entries a pinned table names), so it is not part of src\package.c; it is kept here, compiled by tests	est_driver.c
// (include it after src\setup.c), as a user of zip_walk / zip_extract / zip_name_safe and for the extraction tests.

#define ZIP_MAX_FILES 500
#define ZIP_MAX_ENTRY (32UL * 1024UL * 1024UL)
#define ZIP_MAX_TOTAL (96UL * 1024UL * 1024UL)

typedef struct { const WCHAR *dest; volatile LONG *cancel; int files, entries; DWORD total; int rc; } xall;

static BOOL make_dirs(WCHAR *path, int from)        // creates every folder of path beyond the first `from` characters
{
    int i;
    for (i = from; path[i]; i++) {
        if (path[i] != '\\') continue;
        path[i] = 0;
        if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) { path[i] = '\\'; return FALSE; }
        path[i] = '\\';
    }
    return TRUE;
}

static BOOL xall_visit(const char *name, DWORD nlen, const zipent *e, void *ctx)
{
    xall *x = (xall *)ctx; static WCHAR path[MAX_PATH]; BOOL isdir; int dl = lstrlenW(x->dest), n, i; DWORD a;
    if (*x->cancel) { x->rc = PK_CANCEL; return FALSE; }
    if (!zip_name_safe(name, nlen, &isdir) || (e->flags & 1) || (e->method != 0 && e->method != 8) ||
        ++x->entries > ZIP_MAX_FILES || e->usize > ZIP_MAX_ENTRY || (x->total += e->usize) > ZIP_MAX_TOTAL ||
        dl + 2 + (int)nlen >= MAX_PATH - 1) { x->rc = PK_FORMAT; return FALSE; }
    lstrcpyW(path, x->dest); path[dl] = '\\';
    n = MultiByteToWideChar((e->flags & 0x800) ? CP_UTF8 : CP_OEMCP, 0, name, isdir ? (int)nlen - 1 : (int)nlen, path + dl + 1, MAX_PATH - dl - 2);
    if (n <= 0) { x->rc = PK_FORMAT; return FALSE; }
    path[dl + 1 + n] = 0;
    for (i = dl + 1; path[i]; i++) if (path[i] == '/') path[i] = '\\';
    if (!make_dirs(path, dl + 1) || (isdir && !CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) { x->rc = PK_IO; return FALSE; }
    if (isdir) return TRUE;
    a = GetFileAttributesW(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) { x->rc = PK_FORMAT; return FALSE; }   // a file named like a folder made earlier
    x->rc = zip_extract(e, "", path, NULL, x->cancel);
    if (x->rc == PK_PASSWORD) x->rc = PK_FORMAT;              // CRC or size mismatch: the entry is damaged
    if (x->rc != PK_OK) return FALSE;
    x->files++;
    return TRUE;
}

int zip_extract_all(const WCHAR *zip, const WCHAR *dest, volatile LONG *cancel, int *nfiles)
{
    xall x; int rc;
    zero(&x, sizeof(x)); x.dest = dest; x.cancel = cancel; x.rc = PK_OK;
    rc = zip_walk(zip, xall_visit, &x);
    if (nfiles) *nfiles = x.files;
    if (rc == PK_CANCEL) return x.rc != PK_OK ? x.rc : PK_CANCEL;
    return rc;
}
