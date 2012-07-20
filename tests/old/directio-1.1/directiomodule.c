/* py-directio file : directiomodule.c
   A Python module interface to 'open', 'read' and 'write' on a
   direct I/O context.

   This is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License 
   as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
   .- */

#include <Python.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/utsname.h>
#include <string.h>

static unsigned int alignment;

static inline void
get_alignment_size (void)
{
  struct utsname tmp;
  char twodotfour[] = "2.4", twodotsix[] = "2.6";
  int ret = 0;

  Py_BEGIN_ALLOW_THREADS;
  ret = uname (&tmp);
  Py_END_ALLOW_THREADS;

  if (ret == -1)
    {
      PyErr_SetFromErrno (PyExc_OSError);
      goto end;
    }
  if (!memcmp (twodotfour, tmp.release, 3))
    {
      if (atoi ((tmp.release) + 4) < 10)
	goto ignored;
      alignment = 4096;
    }
  else if (!memcmp (twodotsix, tmp.release, 3))
    alignment = 512;
  else
    goto ignored;
  goto end;

ignored:
  alignment = 0;
  PyErr_Warn (NULL,
	      "the O_DIRECT flag is being ignored (Linux kernel release "
	      "should be 2.4.10 or higher).\nThis module's methods will work exactly the "
	      "same as the os module ones.");
end:
  ;
}

static PyObject *
method_open (PyObject * self, PyObject * args)
{
  int fd;
  char *pathname;
  int flags;
  mode_t mode;

  if (!PyArg_ParseTuple (args, "si|i", &pathname, &flags, &mode))
    return NULL;

  Py_BEGIN_ALLOW_THREADS;
  fd = open (pathname, flags | O_DIRECT, mode);
  Py_END_ALLOW_THREADS;

  if (fd == -1)
    {
      PyErr_SetFromErrnoWithFilename (PyExc_OSError, pathname);
      return NULL;
    }
  else
    {
      return Py_BuildValue ("i", fd);
    }
}

static PyObject *
method_close (PyObject * self, PyObject * args)
{
  int fd;
  int ret;

  if (!PyArg_ParseTuple (args, "i", &fd))
    return NULL;

  Py_BEGIN_ALLOW_THREADS;
  ret = close (fd);
  Py_END_ALLOW_THREADS;

  if (ret == -1)
    {
      PyErr_SetFromErrno (PyExc_OSError);
      return NULL;
    }
  else
    {
      return Py_BuildValue ("");
    }
}

static PyObject *
method_read (PyObject * self, PyObject * args)
{
  int fd;
  void *realbuff = NULL, *alignedbuff = NULL;
  PyObject *pyresult;
  size_t count;

  ssize_t ret;

  if (!PyArg_ParseTuple (args, "iI", &fd, &count))
    return NULL;

  if (count < 0)
    {
      PyErr_SetString (PyExc_ValueError, "transfer size must be positive.");
      return NULL;
    }

  if (alignment && count % alignment)
    {
      if (alignment == 512)
	PyErr_SetString (PyExc_ValueError,
			 "transfer size must be a multiple of a 512.");
      else
	PyErr_SetString (PyExc_ValueError,
			 "transfer size must be a multiple of 4096.");
      return NULL;
    }

  /* alloc and align */
  if (!(realbuff = calloc (alignment + count, 1)))
    {
      return PyErr_NoMemory ();
    }
  if (alignment)
    alignedbuff = 
      (void *) ((((intptr_t) realbuff + alignment - 1) / alignment) *
		alignment);
  else
    alignedbuff = realbuff;

  Py_BEGIN_ALLOW_THREADS;
  ret = read (fd, alignedbuff, count);
  Py_END_ALLOW_THREADS;


  if (ret == -1)
    {
      PyErr_SetFromErrno (PyExc_OSError);
      free (realbuff);
      return NULL;
    }
  else
    {
      pyresult = PyString_FromStringAndSize (alignedbuff, ret);
      free (realbuff);
      return pyresult;
    }
}

static PyObject *
method_write (PyObject * self, PyObject * args)
{
  int fd;
  void *buff = NULL, *realbuff = NULL, *alignedbuff = NULL;
  int count = 0;
  ssize_t ret = 0;

  if (!PyArg_ParseTuple (args, "is#", &fd, &buff, &count))
    return NULL;

  if (alignment && count % alignment)
    {
      PyErr_SetString (PyExc_ValueError,
		       "transfer size must be a multiple of the logical block size of the file system.");
      return NULL;
    }

  /* alloc and align */
  if (!(realbuff = calloc (alignment + count, 1)))
    {
      return PyErr_NoMemory ();
    }
  alignedbuff =
    (void *) ((((intptr_t) realbuff + alignment - 1) / alignment) *
	      alignment);
  memcpy (alignedbuff, buff, count);

  Py_BEGIN_ALLOW_THREADS;
  ret = write (fd, alignedbuff, count);
  Py_END_ALLOW_THREADS;

  free (realbuff);

  if (ret == -1)
    {
      PyErr_SetFromErrno (PyExc_OSError);
      return NULL;
    }
  return Py_BuildValue ("i", ret);
}

static PyMethodDef SpliceTeeMethods[] = {
  {"open", method_open, METH_VARARGS,
   "open(pathname, flags[, mode]) = fd\n"
   "\n"
   "Given a pathname for a file, open() returns a file descriptor, a small, \
non-negative integer for use in subsequent system calls (read(2), write(2), \
lseek(2), fcntl(2), etc.). The file descriptor returned by a successful call \
will be the lowest-numbered file descriptor not currently open for the \
process.\n" "The O_DIRECT flag is automatically added to the 'flags' in argument so as to \
try to minimize cache effects of the I/O to and from this file. In general \
this will degrade  performance, but it is useful in special situations, such \
as when applications do their own caching. File I/O is done directly to/from \
user space buffers. The I/O is synchronous, i.e., at the completion of a \
read(2) or write(2), data is guaranteed to have been transferred.\n" "Under Linux  2.4 transfer sizes, and the alignment of user buffer and file \
offset must all be multiples of the logical block size of the file system.\n" "Under Linux 2.6 alignment to 512-byte boundaries suffices.\n" "Note that on a NFS file system, O_DIRECT flag is ignored.\n" "\n" "Upon success, 'open' returns the new file descriptor.\n"},
  {"read", method_read, METH_VARARGS,
   "read(fd, cout) = string\n"
   "\n"
   "read() attempts to read up to count bytes from file descriptor fd into a \
buffer.\n" "\n" "Upon success, 'read' returns a buffer containing the bytes read.\n"},
  {"write", method_write, METH_VARARGS,
   "write(fd,buf) = sent\n"
   "\n"
   " write() writes up to count bytes to the file referenced by the file \
descriptor fd from the buffer 'buf'.\n" "On a direct I/O context, this will result in data stored directly in hd, \
ignoring the buffer cache.\n"},
  {"close", method_close, METH_VARARGS,
   "close(fd)\n"
   "\n"
   "close() closes a file descriptor, so that it no longer refers to any file \
and may be reused."},
  {NULL, NULL, 0, NULL}		/* Sentinel */
};

static void
insint (PyObject * d, char *name, int value)
{
  PyObject *v = PyInt_FromLong ((long) value);
  if (!v || PyDict_SetItemString (d, name, v))
    PyErr_Clear ();

  Py_XDECREF (v);
}

PyMODINIT_FUNC
initdirectio (void)
{
  PyObject *m, *d;

  get_alignment_size ();

  m = Py_InitModule ("directio", SpliceTeeMethods);
  if (!m)
    return;

  d = PyModule_GetDict (m);

  insint (d, "O_RDONLY", O_RDONLY);
  insint (d, "O_WRONLY", O_WRONLY);
  insint (d, "O_RDWR", O_RDWR);
  insint (d, "O_APPEND", O_APPEND);
  insint (d, "O_CREAT", O_CREAT);
  insint (d, "O_EXCL", O_EXCL);
  insint (d, "O_TRUNC", O_TRUNC);

  PyModule_AddStringConstant (m, "__doc__",
			      "Direct interface to 'open', 'read'"
			      ", 'write' and 'close' system calls on a direct I/O context.");
  PyModule_AddStringConstant (m, "__version__", "1.0");
}
