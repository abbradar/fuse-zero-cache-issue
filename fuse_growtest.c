#define FUSE_USE_VERSION 31

#include <errno.h>
#include <fuse3/fuse_lowlevel.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FILL_BYTE 0xAA
#define GROW_RATE (128 * 1024) /* bytes per second */
#define INIT_SIZE (4096 + 900) /* 4996: straddles page 1 */

#define ROOT_INO 1
#define FILE_INO 2

static struct timespec start_time;
static struct fuse_session *fuse_session;

static size_t current_size(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double dt = (now.tv_sec - start_time.tv_sec) +
              (now.tv_nsec - start_time.tv_nsec) / 1e9;
  return INIT_SIZE + (size_t)(dt * GROW_RATE);
}

static void fill_file_attr(struct stat *st, size_t sz) {
  memset(st, 0, sizeof(*st));
  st->st_ino = FILE_INO;
  st->st_mode = S_IFREG | 0444;
  st->st_nlink = 1;
  st->st_size = sz;
}

static void fill_dir_attr(struct stat *st) {
  memset(st, 0, sizeof(*st));
  st->st_ino = ROOT_INO;
  st->st_mode = S_IFDIR | 0555;
  st->st_nlink = 2;
}

/* ---- low-level ops ---- */

static void gt_init(void *userdata, struct fuse_conn_info *conn) {
  (void)userdata;
  (void)conn;
}

static void gt_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
  if (parent != ROOT_INO || strcmp(name, "testfile") != 0) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  struct fuse_entry_param e = {
      .ino = FILE_INO,
      .attr_timeout = 0,
      .entry_timeout = 0,
  };
  fill_file_attr(&e.attr, current_size());
  fuse_reply_entry(req, &e);
}

static void gt_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info *fi) {
  (void)fi;

  if (ino == ROOT_INO) {
    struct stat st;
    fill_dir_attr(&st);
    fuse_reply_attr(req, &st, 0);
    return;
  }
  if (ino != FILE_INO) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat st;
  size_t sz = current_size();
  fill_file_attr(&st, sz);
  fuse_reply_attr(req, &st, 0);
}

static void gt_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                       struct fuse_file_info *fi) {
  (void)fi;
  if (ino != ROOT_INO) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  struct {
    const char *name;
    fuse_ino_t ino;
    mode_t mode;
  } entries[] = {
      {".", ROOT_INO, S_IFDIR},
      {"..", ROOT_INO, S_IFDIR},
      {"testfile", FILE_INO, S_IFREG},
  };
  int n = sizeof(entries) / sizeof(entries[0]);

  char *buf = calloc(1, size);
  size_t pos = 0;
  for (int i = off; i < n; i++) {
    struct stat st = {.st_ino = entries[i].ino, .st_mode = entries[i].mode, 0};
    size_t ent = fuse_add_direntry(req, buf + pos, size - pos, entries[i].name,
                                   &st, i + 1);
    if (pos + ent > size)
      break;
    pos += ent;
  }
  fuse_reply_buf(req, buf, pos);
  free(buf);
}

static void gt_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  if (ino != FILE_INO) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  fuse_reply_open(req, fi);
}

static void gt_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info *fi) {
  (void)fi;
  if (ino != FILE_INO) {
    fuse_reply_err(req, EIO);
    return;
  }

  size_t sz = current_size();
  if ((size_t)off >= sz) {
    fuse_reply_buf(req, NULL, 0);
    return;
  }
  if (off + size > sz)
    size = sz - off;

  char *buf = malloc(size);
  memset(buf, FILL_BYTE, size);
  fuse_reply_buf(req, buf, size);
  free(buf);
}

static const struct fuse_lowlevel_ops fuse_ops = {
    .init = gt_init,
    .lookup = gt_lookup,
    .getattr = gt_getattr,
    .readdir = gt_readdir,
    .open = gt_open,
    .read = gt_read,
};

int main(int argc, char *argv[]) {
  int ret = 1;

  if (argc < 2) {
    fprintf(stderr, "usage: %s MOUNTPOINT\n", argv[0]);
    return 1;
  }
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  const char *fuse_argv[] = {
      argv[0],
      "-oauto_unmount,allow_other",
  };
  struct fuse_args args = FUSE_ARGS_INIT(2, (char **)fuse_argv);

  fuse_session = fuse_session_new(&args, &fuse_ops, sizeof(fuse_ops), NULL);
  if (!fuse_session)
    return 1;

  if (fuse_set_signal_handlers(fuse_session) != 0)
    goto err_session;

  if (fuse_session_mount(fuse_session, argv[argc - 1]) != 0)
    goto err_signal;

  ret = fuse_session_loop_mt(fuse_session, 0);

  fuse_session_unmount(fuse_session);
err_signal:
  fuse_remove_signal_handlers(fuse_session);
err_session:
  fuse_session_destroy(fuse_session);
  return ret;
}
