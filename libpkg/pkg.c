#include <err.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"
#include "util.h"

static void pkg_free_void(void*);

pkg_t
pkg_type(struct pkg *pkg)
{
	return (pkg->type);
}
const char *
pkg_origin(struct pkg *pkg)
{
	return (sbuf_data(pkg->origin));
}

const char *
pkg_name(struct pkg *pkg)
{
	return (sbuf_data(pkg->name));
}

const char *
pkg_version(struct pkg *pkg)
{
	return (sbuf_data(pkg->version));
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (sbuf_data(pkg->comment));
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (sbuf_data(pkg->desc));
}

struct pkg **
pkg_deps(struct pkg *pkg)
{
	return ((struct pkg **)pkg->deps.data);
}

int
pkg_numdeps(struct pkg *pkg)
{
	return (pkg->deps.len);
}

int
pkg_resolvdeps(struct pkg *pkg, struct pkgdb *db) {
	struct pkg *p;
	struct pkgdb_it *it;
	struct pkg **deps;
	int i;

	deps = pkg_deps(pkg);
	pkg_new(&p);
	for (i = 0; deps[i] != NULL; i++) {
		it = pkgdb_query(db, pkg_origin(deps[i]), MATCH_EXACT);

		if (pkgdb_it_next_pkg(it, &p, MATCH_EXACT) == 0) {
			p->type = PKG_INSTALLED;
			pkg_free(deps[i]);
			deps[i] = p;
		} else {
			deps[i]->type = PKG_NOTFOUND;
		}
		pkgdb_it_free(it);
	}

	return (0);
}

struct pkg **
pkg_rdeps(struct pkg *pkg)
{
	return ((struct pkg **)pkg->rdeps.data);
}

struct pkg_file **
pkg_files(struct pkg *pkg)
{
	return ((struct pkg_file **)pkg->files.data);
}

struct pkg_conflict **
pkg_conflicts(struct pkg *pkg)
{
	return ((struct pkg_conflict **)pkg->conflicts.data);
}

int
pkg_open(const char *path, struct pkg **pkg, int query_flags)
{
	struct archive *a;
	struct archive_entry *ae;
	struct pkg_file *file = NULL;
	int ret;
	int64_t size;
	char *buf;

	/* search for http(s) or ftp(s) */
	if (STARTS_WITH(path, "http://") || STARTS_WITH(path, "https://")
			|| STARTS_WITH(path, "ftp://")) {
		file_fetch(path, "/tmp/bla");
		path = "/tmp/bla";
	}
	(void)query_flags;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, path, 4096) != ARCHIVE_OK) {
		archive_read_finish(a);
		printf("la");
		return (-1);
	}

	/* first patch to check is the archive is corrupted bye the way retreive
	 * informations */

	pkg_new(pkg);
	(*pkg)->type = PKG_FILE;

	array_init(&(*pkg)->deps, 5);
	array_init(&(*pkg)->conflicts, 5);
	array_init(&(*pkg)->files, 10);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		if (!strcmp(archive_entry_pathname(ae),"+DESC")) {
			size = archive_entry_size(ae);
			buf = calloc(1, size+1);
			archive_read_data(a, buf, size);
			sbuf_cat((*pkg)->desc, buf);
			sbuf_finish((*pkg)->desc);
			free(buf);
		}

		if (!strcmp(archive_entry_pathname(ae), "+MANIFEST")) {
			size = archive_entry_size(ae);
			buf = calloc(1, size + 1);
			archive_read_data(a, buf, size);
			pkg_parse_manifest(*pkg, buf);
			free(buf);
		}

		if (archive_entry_pathname(ae)[0] == '+') {
			archive_read_data_skip(a);
			continue;
		}


		pkg_file_new(&file);
		strlcpy(file->path, archive_entry_pathname(ae), sizeof(file->path));
		array_append(&(*pkg)->files, file);

		archive_read_data_skip(a);
	}

	if (ret != ARCHIVE_EOF)
		warn("Archive corrupted");

	archive_read_finish(a);

	return (0);
}

int
pkg_new(struct pkg **pkg)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL)
		err(EXIT_FAILURE, "calloc()");

	(*pkg)->name = sbuf_new_auto();
	(*pkg)->version = sbuf_new_auto();
	(*pkg)->origin = sbuf_new_auto();
	(*pkg)->comment = sbuf_new_auto();
	(*pkg)->desc = sbuf_new_auto();

	return (0);
}

void
pkg_reset(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	sbuf_clear(pkg->name);
	sbuf_clear(pkg->version);
	sbuf_clear(pkg->origin);
	sbuf_clear(pkg->comment);
	sbuf_clear(pkg->desc);

	array_reset(&pkg->deps, &pkg_free_void);
	array_reset(&pkg->rdeps, &pkg_free_void);
	array_reset(&pkg->conflicts, &pkg_conflict_free_void);
	array_reset(&pkg->files, &free);
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	sbuf_delete(pkg->name);
	sbuf_delete(pkg->version);
	sbuf_delete(pkg->origin);
	sbuf_delete(pkg->comment);
	sbuf_delete(pkg->desc);

	array_free(&pkg->deps, &pkg_free_void);
	array_free(&pkg->rdeps, &pkg_free_void);
	array_free(&pkg->conflicts, &pkg_conflict_free_void);
	array_free(&pkg->files, &free);

	free(pkg);
}

static void
pkg_free_void(void *p)
{
	if (p != NULL)
		pkg_free((struct pkg*) p);
}

/* setters */
int
pkg_setname(struct pkg *pkg, const char *name)
{
	if (name == NULL)
		return (-1);

	if (sbuf_done(pkg->name) != 0)
		sbuf_clear(pkg->name);

	sbuf_cat(pkg->name, name);
	sbuf_finish(pkg->name);

	return (0);
}

int
pkg_setversion(struct pkg *pkg, const char *version)
{
	if (version == NULL)
		return (-1);

	if (sbuf_done(pkg->version) != 0)
		sbuf_clear(pkg->version);

	sbuf_cat(pkg->version, version);
	sbuf_finish(pkg->version);

	return (0);
}

int
pkg_setcomment(struct pkg *pkg, const char *comment)
{
	if (comment == NULL)
		return (-1);

	if (sbuf_done(pkg->comment) != 0)
		sbuf_clear(pkg->comment);

	sbuf_cat(pkg->comment, comment);
	sbuf_finish(pkg->comment);

	return (0);
}

int
pkg_setorigin(struct pkg *pkg, const char *origin)
{
	if (origin == NULL)
		return (-1);

	if (sbuf_done(pkg->origin) != 0)
		sbuf_clear(pkg->origin);

	sbuf_cat(pkg->origin, origin);
	sbuf_finish(pkg->origin);

	return (0);
}

int
pkg_setdesc_from_file(struct pkg *pkg, const char *desc_path)
{
	char *buf = NULL;
	int ret = 0;

	if (file_to_buffer(desc_path, &buf) <= 0)
		return (-1);

	ret = pkg_setdesc(pkg, buf);

	free(buf);

	return (ret);
}

int
pkg_setdesc(struct pkg *pkg, const char *desc)
{
	if (desc == NULL)
		return (-1);

	if (sbuf_done(pkg->desc) != 0)
		sbuf_clear(pkg->desc);

	sbuf_cat(pkg->desc, desc);
	sbuf_finish(pkg->desc);

	return (0);
}

int
pkg_adddep(struct pkg *pkg, struct pkg *dep)
{
	if (dep == NULL)
		return (-1);

	array_init(&pkg->deps, 5);
	array_append(&pkg->deps, dep);

	return (0);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256)
{
	struct pkg_file *file;
	if (path == NULL || sha256 == NULL)
		return (-1);

	pkg_file_new(&file);

	strlcpy(file->path, path, sizeof(file->path));
	strlcpy(file->sha256, sha256, sizeof(file->sha256));

	array_init(&pkg->files, 10);
	array_append(&pkg->files, file);

	return (0);
}

int
pkg_addconflict(struct pkg *pkg, const char *glob)
{
	struct pkg_conflict *conflict;

	if (glob == NULL)
		return (-1);

	pkg_conflict_new(&conflict);
	sbuf_cat(conflict->glob, glob);
	sbuf_finish(conflict->glob);

	array_init(&pkg->conflicts, 5);
	array_append(&pkg->conflicts, conflict);

	return (0);
}
