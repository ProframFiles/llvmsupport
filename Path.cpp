//===-- Path.cpp - Implement OS Path Concept ------------------------------===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system Path API.
//
//===----------------------------------------------------------------------===//

#include "Path.hpp"
#include "Endian.hpp"
#include "FileSystem.hpp"
#include "FatalError.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {
  using akj::cStringRef;
  using akj::sys::path::is_separator;

#ifdef _WIN32
  const char *separators = "\\/";
  const char  prefered_separator = '\\';
#else
  const char  separators = '/';
  const char  prefered_separator = '/';
#endif

  cStringRef find_first_component(cStringRef path) {
    // Look for this first component in the following order.
    // * empty (in this case we return an empty string)
    // * either C: or {//,\\}net.
    // * {/,\}
    // * {.,..}
    // * {file,directory}name

    if (path.empty())
      return path;

#ifdef _WIN32
    // C:
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':')
      return path.substr(0, 2);
#endif

    // //net
    if ((path.size() > 2) &&
        is_separator(path[0]) &&
        path[0] == path[1] &&
        !is_separator(path[2])) {
      // Find the next directory separator.
      size_t end = path.find_first_of(separators, 2);
      return path.substr(0, end);
    }

    // {/,\}
    if (is_separator(path[0]))
      return path.substr(0, 1);

    if (path.startswith(".."))
      return path.substr(0, 2);

    if (path[0] == '.')
      return path.substr(0, 1);

    // * {file,directory}name
    size_t end = path.find_first_of(separators);
    return path.substr(0, end);
  }

  size_t filename_pos(cStringRef str) {
    if (str.size() == 2 &&
        is_separator(str[0]) &&
        str[0] == str[1])
      return 0;

    if (str.size() > 0 && is_separator(str[str.size() - 1]))
      return str.size() - 1;

    size_t pos = str.find_last_of(separators, str.size() - 1);

#ifdef _WIN32
    if (pos == cStringRef::npos)
      pos = str.find_last_of(':', str.size() - 2);
#endif

    if (pos == cStringRef::npos ||
        (pos == 1 && is_separator(str[0])))
      return 0;

    return pos + 1;
  }

  size_t root_dir_start(cStringRef str) {
    // case "c:/"
#ifdef _WIN32
    if (str.size() > 2 &&
        str[1] == ':' &&
        is_separator(str[2]))
      return 2;
#endif

    // case "//"
    if (str.size() == 2 &&
        is_separator(str[0]) &&
        str[0] == str[1])
      return cStringRef::npos;

    // case "//net"
    if (str.size() > 3 &&
        is_separator(str[0]) &&
        str[0] == str[1] &&
        !is_separator(str[2])) {
      return str.find_first_of(separators, 2);
    }

    // case "/"
    if (str.size() > 0 && is_separator(str[0]))
      return 0;

    return cStringRef::npos;
  }

  size_t parent_path_end(cStringRef path) {
    size_t end_pos = filename_pos(path);

    bool filename_was_sep = path.size() > 0 && is_separator(path[end_pos]);

    // Skip separators except for root dir.
    size_t root_dir_pos = root_dir_start(path.substr(0, end_pos));

    while(end_pos > 0 &&
          (end_pos - 1) != root_dir_pos &&
          is_separator(path[end_pos - 1]))
      --end_pos;

    if (end_pos == 1 && root_dir_pos == 0 && filename_was_sep)
      return cStringRef::npos;

    return end_pos;
  }
} // end unnamed namespace

enum FSEntity {
  FS_Dir,
  FS_File,
  FS_Name
};

// Implemented in Unix/Path.inc and Windows/Path.inc.
static akj::error_code
createUniqueEntity(const akj::Twine &Model, int &ResultFD,
                   akj::cSmallVectorImpl<char> &ResultPath,
                   bool MakeAbsolute, unsigned Mode, FSEntity Type);

namespace akj {
namespace sys  {
namespace path {

const_iterator begin(cStringRef path) {
  const_iterator i;
  i.Path      = path;
  i.Component = find_first_component(path);
  i.Position  = 0;
  return i;
}

const_iterator end(cStringRef path) {
  const_iterator i;
  i.Path      = path;
  i.Position  = path.size();
  return i;
}

const_iterator &const_iterator::operator++() {
  assert(Position < Path.size() && "Tried to increment past end!");

  // Increment Position to past the current component
  Position += Component.size();

  // Check for end.
  if (Position == Path.size()) {
    Component = cStringRef();
    return *this;
  }

  // Both POSIX and Windows treat paths that begin with exactly two separators
  // specially.
  bool was_net = Component.size() > 2 &&
    is_separator(Component[0]) &&
    Component[1] == Component[0] &&
    !is_separator(Component[2]);

  // Handle separators.
  if (is_separator(Path[Position])) {
    // Root dir.
    if (was_net
#ifdef _WIN32
        // c:/
        || Component.endswith(":")
#endif
        ) {
      Component = Path.substr(Position, 1);
      return *this;
    }

    // Skip extra separators.
    while (Position != Path.size() &&
           is_separator(Path[Position])) {
      ++Position;
    }

    // Treat trailing '/' as a '.'.
    if (Position == Path.size()) {
      --Position;
      Component = ".";
      return *this;
    }
  }

  // Find next component.
  size_t end_pos = Path.find_first_of(separators, Position);
  Component = Path.slice(Position, end_pos);

  return *this;
}

const_iterator &const_iterator::operator--() {
  // If we're at the end and the previous char was a '/', return '.'.
  if (Position == Path.size() &&
      Path.size() > 1 &&
      is_separator(Path[Position - 1])
#ifdef _WIN32
      && Path[Position - 2] != ':'
#endif
      ) {
    --Position;
    Component = ".";
    return *this;
  }

  // Skip separators unless it's the root directory.
  size_t root_dir_pos = root_dir_start(Path);
  size_t end_pos = Position;

  while(end_pos > 0 &&
        (end_pos - 1) != root_dir_pos &&
        is_separator(Path[end_pos - 1]))
    --end_pos;

  // Find next separator.
  size_t start_pos = filename_pos(Path.substr(0, end_pos));
  Component = Path.slice(start_pos, end_pos);
  Position = start_pos;
  return *this;
}

bool const_iterator::operator==(const const_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() &&
         Position == RHS.Position;
}

bool const_iterator::operator!=(const const_iterator &RHS) const {
  return !(*this == RHS);
}

ptrdiff_t const_iterator::operator-(const const_iterator &RHS) const {
  return Position - RHS.Position;
}

const cStringRef root_path(cStringRef path) {
  const_iterator b = begin(path),
                 pos = b,
                 e = end(path);
  if (b != e) {
    bool has_net = b->size() > 2 && is_separator((*b)[0]) && (*b)[1] == (*b)[0];
    bool has_drive =
#ifdef _WIN32
      b->endswith(":");
#else
      false;
#endif

    if (has_net || has_drive) {
      if ((++pos != e) && is_separator((*pos)[0])) {
        // {C:/,//net/}, so get the first two components.
        return path.substr(0, b->size() + pos->size());
      } else {
        // just {C:,//net}, return the first component.
        return *b;
      }
    }

    // POSIX style root directory.
    if (is_separator((*b)[0])) {
      return *b;
    }
  }

  return cStringRef();
}

const cStringRef root_name(cStringRef path) {
  const_iterator b = begin(path),
                 e = end(path);
  if (b != e) {
    bool has_net = b->size() > 2 && is_separator((*b)[0]) && (*b)[1] == (*b)[0];
    bool has_drive =
#ifdef _WIN32
      b->endswith(":");
#else
      false;
#endif

    if (has_net || has_drive) {
      // just {C:,//net}, return the first component.
      return *b;
    }
  }

  // No path or no name.
  return cStringRef();
}

const cStringRef root_directory(cStringRef path) {
  const_iterator b = begin(path),
                 pos = b,
                 e = end(path);
  if (b != e) {
    bool has_net = b->size() > 2 && is_separator((*b)[0]) && (*b)[1] == (*b)[0];
    bool has_drive =
#ifdef _WIN32
      b->endswith(":");
#else
      false;
#endif

    if ((has_net || has_drive) &&
        // {C:,//net}, skip to the next component.
        (++pos != e) && is_separator((*pos)[0])) {
      return *pos;
    }

    // POSIX style root directory.
    if (!has_net && is_separator((*b)[0])) {
      return *b;
    }
  }

  // No path or no root.
  return cStringRef();
}

const cStringRef relative_path(cStringRef path) {
  cStringRef root = root_path(path);
  return path.substr(root.size());
}

void append(cSmallVectorImpl<char> &path, const Twine &a,
                                         const Twine &b,
                                         const Twine &c,
                                         const Twine &d) {
  SmallString<32> a_storage;
  SmallString<32> b_storage;
  SmallString<32> c_storage;
  SmallString<32> d_storage;

  cSmallVector<cStringRef, 4> components;
  if (!a.isTriviallyEmpty()) components.push_back(a.toStringRef(a_storage));
  if (!b.isTriviallyEmpty()) components.push_back(b.toStringRef(b_storage));
  if (!c.isTriviallyEmpty()) components.push_back(c.toStringRef(c_storage));
  if (!d.isTriviallyEmpty()) components.push_back(d.toStringRef(d_storage));

  for (cSmallVectorImpl<cStringRef>::const_iterator i = components.begin(),
                                                  e = components.end();
                                                  i != e; ++i) {
    bool path_has_sep = !path.empty() && is_separator(path[path.size() - 1]);
    bool component_has_sep = !i->empty() && is_separator((*i)[0]);
    bool is_root_name = has_root_name(*i);

    if (path_has_sep) {
      // Strip separators from beginning of component.
      size_t loc = i->find_first_not_of(separators);
      cStringRef c = i->substr(loc);

      // Append it.
      path.append(c.begin(), c.end());
      continue;
    }

    if (!component_has_sep && !(path.empty() || is_root_name)) {
      // Add a separator.
      path.push_back(prefered_separator);
    }

    path.append(i->begin(), i->end());
  }
}

void append(cSmallVectorImpl<char> &path,
            const_iterator begin, const_iterator end) {
  for (; begin != end; ++begin)
    path::append(path, *begin);
}

const cStringRef parent_path(cStringRef path) {
  size_t end_pos = parent_path_end(path);
  if (end_pos == cStringRef::npos)
    return cStringRef();
  else
    return path.substr(0, end_pos);
}

void remove_filename(cSmallVectorImpl<char> &path) {
  size_t end_pos = parent_path_end(cStringRef(path.begin(), path.size()));
  if (end_pos != cStringRef::npos)
    path.set_size(end_pos);
}

void replace_extension(cSmallVectorImpl<char> &path, const Twine &extension) {
  cStringRef p(path.begin(), path.size());
  SmallString<32> ext_storage;
  cStringRef ext = extension.toStringRef(ext_storage);

  // Erase existing extension.
  size_t pos = p.find_last_of('.');
  if (pos != cStringRef::npos && pos >= filename_pos(p))
    path.set_size(pos);

  // Append '.' if needed.
  if (ext.size() > 0 && ext[0] != '.')
    path.push_back('.');

  // Append extension.
  path.append(ext.begin(), ext.end());
}

void native(const Twine &path, cSmallVectorImpl<char> &result) {
  assert((!path.isSingleStringRef() ||
          path.getSingleStringRef().data() != result.data()) &&
         "path and result are not allowed to overlap!");
  // Clear result.
  result.clear();
  path.toVector(result);
  native(result);
}

void native(cSmallVectorImpl<char> &path) {
#ifdef _WIN32
  std::replace(path.begin(), path.end(), '/', '\\');
#endif
}

const cStringRef filename(cStringRef path) {
  return *(--end(path));
}

const cStringRef stem(cStringRef path) {
  cStringRef fname = filename(path);
  size_t pos = fname.find_last_of('.');
  if (pos == cStringRef::npos)
    return fname;
  else
    if ((fname.size() == 1 && fname == ".") ||
        (fname.size() == 2 && fname == ".."))
      return fname;
    else
      return fname.substr(0, pos);
}

const cStringRef extension(cStringRef path) {
  cStringRef fname = filename(path);
  size_t pos = fname.find_last_of('.');
  if (pos == cStringRef::npos)
    return cStringRef();
  else
    if ((fname.size() == 1 && fname == ".") ||
        (fname.size() == 2 && fname == ".."))
      return cStringRef();
    else
      return fname.substr(pos);
}

bool is_separator(char value) {
  switch(value) {
#ifdef _WIN32
    case '\\': // fall through
#endif
    case '/': return true;
    default: return false;
  }
}

void system_temp_directory(bool erasedOnReboot, cSmallVectorImpl<char> &result) {
  result.clear();

#ifdef __APPLE__
  // On Darwin, use DARWIN_USER_TEMP_DIR or DARWIN_USER_CACHE_DIR.
  int ConfName = erasedOnReboot? _CS_DARWIN_USER_TEMP_DIR
                               : _CS_DARWIN_USER_CACHE_DIR;
  size_t ConfLen = confstr(ConfName, 0, 0);
  if (ConfLen > 0) {
    do {
      result.resize(ConfLen);
      ConfLen = confstr(ConfName, result.data(), result.size());
    } while (ConfLen > 0 && ConfLen != result.size());

    if (ConfLen > 0) {
      assert(result.back() == 0);
      result.pop_back();
      return;
    }

    result.clear();
  }
#endif

  // Check whether the temporary directory is specified by an environment
  // variable.
  const char *EnvironmentVariable;
#ifdef _WIN32
  EnvironmentVariable = "TEMP";
#else
  EnvironmentVariable = "TMPDIR";
#endif
  if (char *RequestedDir = getenv(EnvironmentVariable)) {
    result.append(RequestedDir, RequestedDir + strlen(RequestedDir));
    return;
  }

  // Fall back to a system default.
  const char *DefaultResult;
#ifdef _WIN32
  (void)erasedOnReboot;
  DefaultResult = "C:\\TEMP";
#else
  if (erasedOnReboot)
    DefaultResult = "/tmp";
  else
    DefaultResult = "/var/tmp";
#endif
  result.append(DefaultResult, DefaultResult + strlen(DefaultResult));
}

bool has_root_name(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !root_name(p).empty();
}

bool has_root_directory(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !root_directory(p).empty();
}

bool has_root_path(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !root_path(p).empty();
}

bool has_relative_path(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !relative_path(p).empty();
}

bool has_filename(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !filename(p).empty();
}

bool has_parent_path(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !parent_path(p).empty();
}

bool has_stem(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !stem(p).empty();
}

bool has_extension(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  return !extension(p).empty();
}

bool is_absolute(const Twine &path) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  bool rootDir = has_root_directory(p),
#ifdef _WIN32
       rootName = has_root_name(p);
#else
       rootName = true;
#endif

  return rootDir && rootName;
}

bool is_relative(const Twine &path) {
  return !is_absolute(path);
}

} // end namespace path

namespace fs {

error_code getUniqueID(const Twine Path, UniqueID &Result) {
  file_status Status;
  error_code EC = status(Path, Status);
  if (EC)
    return EC;
  Result = Status.getUniqueID();
  return error_code::success();
}

error_code createUniqueFile(const Twine &Model, int &ResultFd,
                            cSmallVectorImpl<char> &ResultPath, unsigned Mode) {
  return createUniqueEntity(Model, ResultFd, ResultPath, false, Mode, FS_File);
}

error_code createUniqueFile(const Twine &Model,
                            cSmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Model, Dummy, ResultPath, false, 0, FS_Name);
}

static error_code createTemporaryFile(const Twine &Model, int &ResultFD,
                                      akj::cSmallVectorImpl<char> &ResultPath,
                                      FSEntity Type) {
  SmallString<128> Storage;
  cStringRef P = Model.toNullTerminatedStringRef(Storage);
  assert(P.find_first_of(separators) == cStringRef::npos &&
         "Model must be a simple filename.");
  // Use P.begin() so that createUniqueEntity doesn't need to recreate Storage.
  return createUniqueEntity(P.begin(), ResultFD, ResultPath,
                            true, owner_read | owner_write, Type);
}

static error_code
createTemporaryFile(const Twine &Prefix, cStringRef Suffix, int &ResultFD,
                    akj::cSmallVectorImpl<char> &ResultPath,
                    FSEntity Type) {
  const char *Middle = Suffix.empty() ? "-%%%%%%" : "-%%%%%%.";
  return createTemporaryFile(Prefix + Middle + Suffix, ResultFD, ResultPath,
                             Type);
}


error_code createTemporaryFile(const Twine &Prefix, cStringRef Suffix,
                               int &ResultFD,
                               cSmallVectorImpl<char> &ResultPath) {
  return createTemporaryFile(Prefix, Suffix, ResultFD, ResultPath, FS_File);
}

error_code createTemporaryFile(const Twine &Prefix, cStringRef Suffix,
                               cSmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createTemporaryFile(Prefix, Suffix, Dummy, ResultPath, FS_Name);
}


// This is a mkdtemp with a different pattern. We use createUniqueEntity mostly
// for consistency. We should try using mkdtemp.
error_code createUniqueDirectory(const Twine &Prefix,
                                 cSmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Prefix + "-%%%%%%", Dummy, ResultPath,
                            true, 0, FS_Dir);
}

error_code make_absolute(cSmallVectorImpl<char> &path) {
  cStringRef p(path.data(), path.size());

  bool rootDirectory = path::has_root_directory(p),
#ifdef _WIN32
       rootName = path::has_root_name(p);
#else
       rootName = true;
#endif

  // Already absolute.
  if (rootName && rootDirectory)
    return error_code::success();

  // All of the following conditions will need the current directory.
  SmallString<128> current_dir;
  if (error_code ec = current_path(current_dir)) return ec;

  // Relative path. Prepend the current directory.
  if (!rootName && !rootDirectory) {
    // Append path to the current directory.
    path::append(current_dir, p);
    // Set path to the result.
    path.swap(current_dir);
    return error_code::success();
  }

  if (!rootName && rootDirectory) {
    cStringRef cdrn = path::root_name(current_dir);
    SmallString<128> curDirRootName(cdrn.begin(), cdrn.end());
    path::append(curDirRootName, p);
    // Set path to the result.
    path.swap(curDirRootName);
    return error_code::success();
  }

  if (rootName && !rootDirectory) {
    cStringRef pRootName      = path::root_name(p);
    cStringRef bRootDirectory = path::root_directory(current_dir);
    cStringRef bRelativePath  = path::relative_path(current_dir);
    cStringRef pRelativePath  = path::relative_path(p);

    SmallString<128> res;
    path::append(res, pRootName, bRootDirectory, bRelativePath, pRelativePath);
    path.swap(res);
    return error_code::success();
  }

  FatalError::Die("All rootName and rootDirectory combinations should have "
                   "occurred above!");
}

error_code create_directories(const Twine &path, bool &existed) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  cStringRef parent = path::parent_path(p);
  if (!parent.empty()) {
    bool parent_exists;
    if (error_code ec = fs::exists(parent, parent_exists)) return ec;

    if (!parent_exists)
      if (error_code ec = create_directories(parent, existed)) return ec;
  }

  return create_directory(p, existed);
}

bool exists(file_status status) {
  return status_known(status) && status.type() != file_type::file_not_found;
}

bool status_known(file_status s) {
  return s.type() != file_type::status_error;
}

bool is_directory(file_status status) {
  return status.type() == file_type::directory_file;
}

error_code is_directory(const Twine &path, bool &result) {
  file_status st;
  if (error_code ec = status(path, st))
    return ec;
  result = is_directory(st);
  return error_code::success();
}

bool is_regular_file(file_status status) {
  return status.type() == file_type::regular_file;
}

error_code is_regular_file(const Twine &path, bool &result) {
  file_status st;
  if (error_code ec = status(path, st))
    return ec;
  result = is_regular_file(st);
  return error_code::success();
}

bool is_symlink(file_status status) {
  return status.type() == file_type::symlink_file;
}

error_code is_symlink(const Twine &path, bool &result) {
  file_status st;
  if (error_code ec = status(path, st))
    return ec;
  result = is_symlink(st);
  return error_code::success();
}

bool is_other(file_status status) {
  return exists(status) &&
         !is_regular_file(status) &&
         !is_directory(status) &&
         !is_symlink(status);
}

void directory_entry::replace_filename(const Twine &filename, file_status st) {
  SmallString<128> path(Path.begin(), Path.end());
  path::remove_filename(path);
  path::append(path, filename);
  Path = path.str();
  Status = st;
}

error_code has_magic(const Twine &path, const Twine &magic, bool &result) {
  SmallString<32>  MagicStorage;
  cStringRef Magic = magic.toStringRef(MagicStorage);
  SmallString<32> Buffer;

  if (error_code ec = get_magic(path, Magic.size(), Buffer)) {
    if (ec == errc::value_too_large) {
      // Magic.size() > file_size(Path).
      result = false;
      return error_code::success();
    }
    return ec;
  }

  result = Magic == Buffer;
  return error_code::success();
}

/// @brief Identify the magic in magic.
  file_magic identify_magic(cStringRef Magic) {
  if (Magic.size() < 4)
    return file_magic::unknown;
  switch ((unsigned char)Magic[0]) {
    case 0xDE:  // 0x0B17C0DE = BC wraper
      if (Magic[1] == (char)0xC0 && Magic[2] == (char)0x17 &&
          Magic[3] == (char)0x0B)
        return file_magic::bitcode;
      break;
    case 'B':
      if (Magic[1] == 'C' && Magic[2] == (char)0xC0 && Magic[3] == (char)0xDE)
        return file_magic::bitcode;
      break;
    case '!':
      if (Magic.size() >= 8)
        if (memcmp(Magic.data(),"!<arch>\n",8) == 0)
          return file_magic::archive;
      break;

    case '\177':
      if (Magic.size() >= 18 && Magic[1] == 'E' && Magic[2] == 'L' &&
          Magic[3] == 'F') {
        bool Data2MSB = Magic[5] == 2;
        unsigned high = Data2MSB ? 16 : 17;
        unsigned low  = Data2MSB ? 17 : 16;
        if (Magic[high] == 0)
          switch (Magic[low]) {
            default: break;
            case 1: return file_magic::elf_relocatable;
            case 2: return file_magic::elf_executable;
            case 3: return file_magic::elf_shared_object;
            case 4: return file_magic::elf_core;
          }
      }
      break;

    case 0xCA:
      if (Magic[1] == char(0xFE) && Magic[2] == char(0xBA) &&
          Magic[3] == char(0xBE)) {
        // This is complicated by an overlap with Java class files.
        // See the Mach-O section in /usr/share/file/magic for details.
        if (Magic.size() >= 8 && Magic[7] < 43)
          return file_magic::macho_universal_binary;
      }
      break;

      // The two magic numbers for mach-o are:
      // 0xfeedface - 32-bit mach-o
      // 0xfeedfacf - 64-bit mach-o
    case 0xFE:
    case 0xCE:
    case 0xCF: {
      uint16_t type = 0;
      if (Magic[0] == char(0xFE) && Magic[1] == char(0xED) &&
          Magic[2] == char(0xFA) &&
          (Magic[3] == char(0xCE) || Magic[3] == char(0xCF))) {
        /* Native endian */
        if (Magic.size() >= 16) type = Magic[14] << 8 | Magic[15];
      } else if ((Magic[0] == char(0xCE) || Magic[0] == char(0xCF)) &&
                 Magic[1] == char(0xFA) && Magic[2] == char(0xED) &&
                 Magic[3] == char(0xFE)) {
        /* Reverse endian */
        if (Magic.size() >= 14) type = Magic[13] << 8 | Magic[12];
      }
      switch (type) {
        default: break;
        case 1: return file_magic::macho_object;
        case 2: return file_magic::macho_executable;
        case 3: return file_magic::macho_fixed_virtual_memory_shared_lib;
        case 4: return file_magic::macho_core;
        case 5: return file_magic::macho_preload_executable;
        case 6: return file_magic::macho_dynamically_linked_shared_lib;
        case 7: return file_magic::macho_dynamic_linker;
        case 8: return file_magic::macho_bundle;
        case 9: return file_magic::macho_dynamic_linker;
        case 10: return file_magic::macho_dsym_companion;
      }
      break;
    }
    case 0xF0: // PowerPC Windows
    case 0x83: // Alpha 32-bit
    case 0x84: // Alpha 64-bit
    case 0x66: // MPS R4000 Windows
    case 0x50: // mc68K
    case 0x4c: // 80386 Windows
      if (Magic[1] == 0x01)
        return file_magic::coff_object;

    case 0x90: // PA-RISC Windows
    case 0x68: // mc68K Windows
      if (Magic[1] == 0x02)
        return file_magic::coff_object;
      break;

    case 0x4d: // Possible MS-DOS stub on Windows PE file
      if (Magic[1] == 0x5a) {
        uint32_t off =
          *reinterpret_cast<const support::ulittle32_t*>(Magic.data() + 0x3c);
        // PE/COFF file, either EXE or DLL.
        if (off < Magic.size() && memcmp(Magic.data() + off, "PE\0\0",4) == 0)
          return file_magic::pecoff_executable;
      }
      break;

    case 0x64: // x86-64 Windows.
      if (Magic[1] == char(0x86))
        return file_magic::coff_object;
      break;

    default:
      break;
  }
  return file_magic::unknown;
}

error_code identify_magic(const Twine &path, file_magic &result) {
  SmallString<32> Magic;
  error_code ec = get_magic(path, Magic.capacity(), Magic);
  if (ec && ec != errc::value_too_large)
    return ec;

  result = identify_magic(Magic);
  return error_code::success();
}

namespace {
error_code remove_all_r(cStringRef path, file_type ft, uint32_t &count) {
  if (ft == file_type::directory_file) {
    // This code would be a lot better with exceptions ;/.
    error_code ec;
    directory_iterator i(path, ec);
    if (ec) return ec;
    for (directory_iterator e; i != e; i.increment(ec)) {
      if (ec) return ec;
      file_status st;
      if (error_code ec = i->status(st)) return ec;
      if (error_code ec = remove_all_r(i->path(), st.type(), count)) return ec;
    }
    bool obviously_this_exists;
    if (error_code ec = remove(path, obviously_this_exists)) return ec;
    assert(obviously_this_exists);
    ++count; // Include the directory itself in the items removed.
  } else {
    bool obviously_this_exists;
    if (error_code ec = remove(path, obviously_this_exists)) return ec;
    assert(obviously_this_exists);
    ++count;
  }

  return error_code::success();
}
} // end unnamed namespace

error_code remove_all(const Twine &path, uint32_t &num_removed) {
  SmallString<128> path_storage;
  cStringRef p = path.toStringRef(path_storage);

  file_status fs;
  if (error_code ec = status(path, fs))
    return ec;
  num_removed = 0;
  return remove_all_r(p, fs.type(), num_removed);
}

error_code directory_entry::status(file_status &result) const {
  return fs::status(Path, result);
}

} // end namespace fs
} // end namespace sys
} // end namespace akj

// Include the truly platform-specific parts.

#if defined(_WIN32)

#include "STLExtras.hpp"
#include "SystemSpecific/Windows.hpp"

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

#undef max

// MinGW doesn't define this.
#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

#ifdef _MSC_VER
# pragma comment(lib, "advapi32.lib")  // This provides CryptAcquireContextW.
#endif

using namespace akj;

using akj::sys::windows::UTF8ToUTF16;
using akj::sys::windows::UTF16ToUTF8;

namespace {
  typedef BOOLEAN (WINAPI *PtrCreateSymbolicLinkW)(
    /*__in*/ LPCWSTR lpSymlinkFileName,
    /*__in*/ LPCWSTR lpTargetFileName,
    /*__in*/ DWORD dwFlags);

  PtrCreateSymbolicLinkW create_symbolic_link_api = PtrCreateSymbolicLinkW(
    ::GetProcAddress(::GetModuleHandleA("kernel32.dll"),
                     "CreateSymbolicLinkW"));

  error_code TempDir(cSmallVectorImpl<wchar_t> &result) {
  retry_temp_dir:
    DWORD len = ::GetTempPathW(result.capacity(), result.begin());

    if (len == 0)
      return windows_error(::GetLastError());

    if (len > result.capacity()) {
      result.reserve(len);
      goto retry_temp_dir;
    }

    result.set_size(len);
    return error_code::success();
  }

  bool is_separator(const wchar_t value) {
    switch (value) {
    case L'\\':
    case L'/':
      return true;
    default:
      return false;
    }
  }
}

// FIXME: mode should be used here and default to user r/w only,
// it currently comes in as a UNIX mode.
static error_code createUniqueEntity(const Twine &model, int &result_fd,
                                     cSmallVectorImpl<char> &result_path,
                                     bool makeAbsolute, unsigned mode,
                                     FSEntity Type) {
  // Use result_path as temp storage.
  result_path.set_size(0);
  cStringRef m = model.toStringRef(result_path);

  cSmallVector<wchar_t, 128> model_utf16;
  if (error_code ec = UTF8ToUTF16(m, model_utf16)) return ec;

  if (makeAbsolute) {
    // Make model absolute by prepending a temp directory if it's not already.
    bool absolute = sys::path::is_absolute(m);

    if (!absolute) {
      cSmallVector<wchar_t, 64> temp_dir;
      if (error_code ec = TempDir(temp_dir)) return ec;
      // Handle c: by removing it.
      if (model_utf16.size() > 2 && model_utf16[1] == L':') {
        model_utf16.erase(model_utf16.begin(), model_utf16.begin() + 2);
      }
      model_utf16.insert(model_utf16.begin(), temp_dir.begin(), temp_dir.end());
    }
  }

  // Replace '%' with random chars. From here on, DO NOT modify model. It may be
  // needed if the randomly chosen path already exists.
  cSmallVector<wchar_t, 128> random_path_utf16;

  // Get a Crypto Provider for CryptGenRandom.
  HCRYPTPROV HCPC;
  if (!::CryptAcquireContextW(&HCPC,
                              NULL,
                              NULL,
                              PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT))
    return windows_error(::GetLastError());
  ScopedCryptContext CryptoProvider(HCPC);

retry_random_path:
  random_path_utf16.set_size(0);
  for (cSmallVectorImpl<wchar_t>::const_iterator i = model_utf16.begin(),
                                                e = model_utf16.end();
                                                i != e; ++i) {
    if (*i == L'%') {
      BYTE val = 0;
      if (!::CryptGenRandom(CryptoProvider, 1, &val))
          return windows_error(::GetLastError());
      random_path_utf16.push_back("0123456789abcdef"[val & 15]);
    }
    else
      random_path_utf16.push_back(*i);
  }
  // Make random_path_utf16 null terminated.
  random_path_utf16.push_back(0);
  random_path_utf16.pop_back();

  HANDLE TempFileHandle = INVALID_HANDLE_VALUE;

  switch (Type) {
  case FS_File: {
    // Try to create + open the path.
    TempFileHandle =
        ::CreateFileW(random_path_utf16.begin(), GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ, NULL,
                      // Return ERROR_FILE_EXISTS if the file
                      // already exists.
                      CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (TempFileHandle == INVALID_HANDLE_VALUE) {
      // If the file existed, try again, otherwise, error.
      error_code ec = windows_error(::GetLastError());
      if (ec == windows_error::file_exists)
        goto retry_random_path;

      return ec;
    }

    // Convert the Windows API file handle into a C-runtime handle.
    int fd = ::_open_osfhandle(intptr_t(TempFileHandle), 0);
    if (fd == -1) {
      ::CloseHandle(TempFileHandle);
      ::DeleteFileW(random_path_utf16.begin());
      // MSDN doesn't say anything about _open_osfhandle setting errno or
      // GetLastError(), so just return invalid_handle.
      return windows_error::invalid_handle;
    }

    result_fd = fd;
    break;
  }

  case FS_Name: {
    DWORD attributes = ::GetFileAttributesW(random_path_utf16.begin());
    if (attributes != INVALID_FILE_ATTRIBUTES)
      goto retry_random_path;
    error_code EC = make_error_code(windows_error(::GetLastError()));
    if (EC != windows_error::file_not_found &&
        EC != windows_error::path_not_found)
      return EC;
    break;
  }

  case FS_Dir:
    if (!::CreateDirectoryW(random_path_utf16.begin(), NULL)) {
      error_code EC = windows_error(::GetLastError());
      if (EC != windows_error::already_exists)
        return EC;
      goto retry_random_path;
    }
    break;
  }

  // Set result_path to the utf-8 representation of the path.
  if (error_code ec = UTF16ToUTF8(random_path_utf16.begin(),
                                  random_path_utf16.size(), result_path)) {
    switch (Type) {
    case FS_File:
      ::CloseHandle(TempFileHandle);
      ::DeleteFileW(random_path_utf16.begin());
    case FS_Name:
      break;
    case FS_Dir:
      ::RemoveDirectoryW(random_path_utf16.begin());
      break;
    }
    return ec;
  }

  return error_code::success();
}

namespace akj {
namespace sys  {
namespace fs {

std::string getMainExecutable(const char *argv0, void *MainExecAddr) {
  char pathname[MAX_PATH];
  DWORD ret = ::GetModuleFileNameA(NULL, pathname, MAX_PATH);
  return ret != MAX_PATH ? pathname : "";
}

UniqueID file_status::getUniqueID() const {
  // The file is uniquely identified by the volume serial number along
  // with the 64-bit file identifier.
  uint64_t FileID = (static_cast<uint64_t>(FileIndexHigh) << 32ULL) |
                    static_cast<uint64_t>(FileIndexLow);

  return UniqueID(VolumeSerialNumber, FileID);
}

TimeValue file_status::getLastModificationTime() const {
  ULARGE_INTEGER UI;
  UI.LowPart = LastWriteTimeLow;
  UI.HighPart = LastWriteTimeHigh;

  TimeValue Ret;
  Ret.fromWin32Time(UI.QuadPart);
  return Ret;
}

error_code current_path(cSmallVectorImpl<char> &result) {
  cSmallVector<wchar_t, 128> cur_path;
  cur_path.reserve(128);
retry_cur_dir:
  DWORD len = ::GetCurrentDirectoryW(cur_path.capacity(), cur_path.data());

  // A zero return value indicates a failure other than insufficient space.
  if (len == 0)
    return windows_error(::GetLastError());

  // If there's insufficient space, the len returned is larger than the len
  // given.
  if (len > cur_path.capacity()) {
    cur_path.reserve(len);
    goto retry_cur_dir;
  }

  cur_path.set_size(len);
  return UTF16ToUTF8(cur_path.begin(), cur_path.size(), result);
}

error_code create_directory(const Twine &path, bool &existed) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  if (error_code ec = UTF8ToUTF16(path.toStringRef(path_storage),
                                  path_utf16))
    return ec;

  if (!::CreateDirectoryW(path_utf16.begin(), NULL)) {
    error_code ec = windows_error(::GetLastError());
    if (ec == windows_error::already_exists)
      existed = true;
    else
      return ec;
  } else
    existed = false;

  return error_code::success();
}

error_code create_hard_link(const Twine &to, const Twine &from) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toStringRef(from_storage);
  cStringRef t = to.toStringRef(to_storage);

  // Convert to utf-16.
  cSmallVector<wchar_t, 128> wide_from;
  cSmallVector<wchar_t, 128> wide_to;
  if (error_code ec = UTF8ToUTF16(f, wide_from)) return ec;
  if (error_code ec = UTF8ToUTF16(t, wide_to)) return ec;

  if (!::CreateHardLinkW(wide_from.begin(), wide_to.begin(), NULL))
    return windows_error(::GetLastError());

  return error_code::success();
}

error_code create_symlink(const Twine &to, const Twine &from) {
  // Only do it if the function is available at runtime.
  if (!create_symbolic_link_api)
    return make_error_code(errc::function_not_supported);

  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toStringRef(from_storage);
  cStringRef t = to.toStringRef(to_storage);

  // Convert to utf-16.
  cSmallVector<wchar_t, 128> wide_from;
  cSmallVector<wchar_t, 128> wide_to;
  if (error_code ec = UTF8ToUTF16(f, wide_from)) return ec;
  if (error_code ec = UTF8ToUTF16(t, wide_to)) return ec;

  if (!create_symbolic_link_api(wide_from.begin(), wide_to.begin(), 0))
    return windows_error(::GetLastError());

  return error_code::success();
}

error_code remove(const Twine &path, bool &existed) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  file_status st;
  error_code EC = status(path, st);
  if (EC) {
    if (EC == windows_error::file_not_found ||
        EC == windows_error::path_not_found) {
      existed = false;
      return error_code::success();
    }
    return EC;
  }

  if (error_code ec = UTF8ToUTF16(path.toStringRef(path_storage),
                                  path_utf16))
    return ec;

  if (st.type() == file_type::directory_file) {
    if (!::RemoveDirectoryW(c_str(path_utf16))) {
      error_code ec = windows_error(::GetLastError());
      if (ec != windows_error::file_not_found)
        return ec;
      existed = false;
    } else
      existed = true;
  } else {
    if (!::DeleteFileW(c_str(path_utf16))) {
      error_code ec = windows_error(::GetLastError());
      if (ec != windows_error::file_not_found)
        return ec;
      existed = false;
    } else
      existed = true;
  }

  return error_code::success();
}

error_code rename(const Twine &from, const Twine &to) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toStringRef(from_storage);
  cStringRef t = to.toStringRef(to_storage);

  // Convert to utf-16.
  cSmallVector<wchar_t, 128> wide_from;
  cSmallVector<wchar_t, 128> wide_to;
  if (error_code ec = UTF8ToUTF16(f, wide_from)) return ec;
  if (error_code ec = UTF8ToUTF16(t, wide_to)) return ec;

  error_code ec = error_code::success();
  for (int i = 0; i < 2000; i++) {
    if (::MoveFileExW(wide_from.begin(), wide_to.begin(),
                      MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
      return error_code::success();
    ec = windows_error(::GetLastError());
    if (ec != windows_error::access_denied)
      break;
    // Retry MoveFile() at ACCESS_DENIED.
    // System scanners (eg. indexer) might open the source file when
    // It is written and closed.
    ::Sleep(1);
  }

  return ec;
}

error_code resize_file(const Twine &path, uint64_t size) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  if (error_code ec = UTF8ToUTF16(path.toStringRef(path_storage),
                                  path_utf16))
    return ec;

  int fd = ::_wopen(path_utf16.begin(), O_BINARY | _O_RDWR, S_IWRITE);
  if (fd == -1)
    return error_code(errno, generic_category());
#ifdef HAVE__CHSIZE_S
  errno_t error = ::_chsize_s(fd, size);
#else
  errno_t error = ::_chsize(fd, size);
#endif
  ::close(fd);
  return error_code(error, generic_category());
}

error_code exists(const Twine &path, bool &result) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  if (error_code ec = UTF8ToUTF16(path.toStringRef(path_storage),
                                  path_utf16))
    return ec;

  DWORD attributes = ::GetFileAttributesW(path_utf16.begin());

  if (attributes == INVALID_FILE_ATTRIBUTES) {
    // See if the file didn't actually exist.
    error_code ec = make_error_code(windows_error(::GetLastError()));
    if (ec != windows_error::file_not_found &&
        ec != windows_error::path_not_found)
      return ec;
    result = false;
  } else
    result = true;
  return error_code::success();
}

bool can_write(const Twine &Path) {
  // FIXME: take security attributes into account.
  SmallString<128> PathStorage;
  cSmallVector<wchar_t, 128> PathUtf16;

  if (UTF8ToUTF16(Path.toStringRef(PathStorage), PathUtf16))
    return false;

  DWORD Attr = ::GetFileAttributesW(PathUtf16.begin());
  return (Attr != INVALID_FILE_ATTRIBUTES) && !(Attr & FILE_ATTRIBUTE_READONLY);
}

bool can_execute(const Twine &Path) {
  SmallString<128> PathStorage;
  cSmallVector<wchar_t, 128> PathUtf16;

  if (UTF8ToUTF16(Path.toStringRef(PathStorage), PathUtf16))
    return false;

  DWORD Attr = ::GetFileAttributesW(PathUtf16.begin());
  return Attr != INVALID_FILE_ATTRIBUTES;
}

bool equivalent(file_status A, file_status B) {
  assert(status_known(A) && status_known(B));
  return A.FileIndexHigh      == B.FileIndexHigh &&
         A.FileIndexLow       == B.FileIndexLow &&
         A.FileSizeHigh       == B.FileSizeHigh &&
         A.FileSizeLow        == B.FileSizeLow &&
         A.LastWriteTimeHigh  == B.LastWriteTimeHigh &&
         A.LastWriteTimeLow   == B.LastWriteTimeLow &&
         A.VolumeSerialNumber == B.VolumeSerialNumber;
}

error_code equivalent(const Twine &A, const Twine &B, bool &result) {
  file_status fsA, fsB;
  if (error_code ec = status(A, fsA)) return ec;
  if (error_code ec = status(B, fsB)) return ec;
  result = equivalent(fsA, fsB);
  return error_code::success();
}

static bool isReservedName(cStringRef path) {
  // This list of reserved names comes from MSDN, at:
  // http://msdn.microsoft.com/en-us/library/aa365247%28v=vs.85%29.aspx
  static const char *sReservedNames[] = { "nul", "con", "prn", "aux",
                              "com1", "com2", "com3", "com4", "com5", "com6",
                              "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                              "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };

  // First, check to see if this is a device namespace, which always
  // starts with \\.\, since device namespaces are not legal file paths.
  if (path.startswith("\\\\.\\"))
    return true;

  // Then compare against the list of ancient reserved names
  for (size_t i = 0; i < array_lengthof(sReservedNames); ++i) {
    if (path.equals_lower(sReservedNames[i]))
      return true;
  }

  // The path isn't what we consider reserved.
  return false;
}

static error_code getStatus(HANDLE FileHandle, file_status &Result) {
  if (FileHandle == INVALID_HANDLE_VALUE)
    goto handle_status_error;

  switch (::GetFileType(FileHandle)) {
  default:
    FatalError::Die("Don't know anything about this file type");
  case FILE_TYPE_UNKNOWN: {
    DWORD Err = ::GetLastError();
    if (Err != NO_ERROR)
      return windows_error(Err);
    Result = file_status(file_type::type_unknown);
    return error_code::success();
  }
  case FILE_TYPE_DISK:
    break;
  case FILE_TYPE_CHAR:
    Result = file_status(file_type::character_file);
    return error_code::success();
  case FILE_TYPE_PIPE:
    Result = file_status(file_type::fifo_file);
    return error_code::success();
  }

  BY_HANDLE_FILE_INFORMATION Info;
  if (!::GetFileInformationByHandle(FileHandle, &Info))
    goto handle_status_error;

  {
    file_type Type = (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                         ? file_type::directory_file
                         : file_type::regular_file;
    Result =
        file_status(Type, Info.ftLastWriteTime.dwHighDateTime,
                    Info.ftLastWriteTime.dwLowDateTime,
                    Info.dwVolumeSerialNumber, Info.nFileSizeHigh,
                    Info.nFileSizeLow, Info.nFileIndexHigh, Info.nFileIndexLow);
    return error_code::success();
  }

handle_status_error:
  error_code EC = windows_error(::GetLastError());
  if (EC == windows_error::file_not_found ||
      EC == windows_error::path_not_found)
    Result = file_status(file_type::file_not_found);
  else if (EC == windows_error::sharing_violation)
    Result = file_status(file_type::type_unknown);
  else
    Result = file_status(file_type::status_error);
  return EC;
}

error_code status(const Twine &path, file_status &result) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  cStringRef path8 = path.toStringRef(path_storage);
  if (isReservedName(path8)) {
    result = file_status(file_type::character_file);
    return error_code::success();
  }

  if (error_code ec = UTF8ToUTF16(path8, path_utf16))
    return ec;

  DWORD attr = ::GetFileAttributesW(path_utf16.begin());
  if (attr == INVALID_FILE_ATTRIBUTES)
    return getStatus(INVALID_HANDLE_VALUE, result);

  // Handle reparse points.
  if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    ScopedFileHandle h(
      ::CreateFileW(path_utf16.begin(),
                    0, // Attributes only.
                    FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    0));
    if (!h)
      return getStatus(INVALID_HANDLE_VALUE, result);
  }

  ScopedFileHandle h(
      ::CreateFileW(path_utf16.begin(), 0, // Attributes only.
                    FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0));
    if (!h)
      return getStatus(INVALID_HANDLE_VALUE, result);

    return getStatus(h, result);
}

error_code status(int FD, file_status &Result) {
  HANDLE FileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  return getStatus(FileHandle, Result);
}

error_code setLastModificationAndAccessTime(int FD, TimeValue Time) {
  ULARGE_INTEGER UI;
  UI.QuadPart = Time.toWin32Time();
  FILETIME FT;
  FT.dwLowDateTime = UI.LowPart;
  FT.dwHighDateTime = UI.HighPart;
  HANDLE FileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (!SetFileTime(FileHandle, NULL, &FT, &FT))
    return windows_error(::GetLastError());
  return error_code::success();
}

error_code get_magic(const Twine &path, uint32_t len,
                     cSmallVectorImpl<char> &result) {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;
  result.set_size(0);

  // Convert path to UTF-16.
  if (error_code ec = UTF8ToUTF16(path.toStringRef(path_storage),
                                  path_utf16))
    return ec;

  // Open file.
  HANDLE file = ::CreateFileW(c_str(path_utf16),
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_READONLY,
                              NULL);
  if (file == INVALID_HANDLE_VALUE)
    return windows_error(::GetLastError());

  // Allocate buffer.
  result.reserve(len);

  // Get magic!
  DWORD bytes_read = 0;
  BOOL read_success = ::ReadFile(file, result.data(), len, &bytes_read, NULL);
  error_code ec = windows_error(::GetLastError());
  ::CloseHandle(file);
  if (!read_success || (bytes_read != len)) {
    // Set result size to the number of bytes read if it's valid.
    if (bytes_read <= len)
      result.set_size(bytes_read);
    // ERROR_HANDLE_EOF is mapped to errc::value_too_large.
    return ec;
  }

  result.set_size(len);
  return error_code::success();
}

error_code mapped_file_region::init(int FD, bool CloseFD, uint64_t Offset) {
  FileDescriptor = FD;
  // Make sure that the requested size fits within SIZE_T.
  if (Size > std::numeric_limits<SIZE_T>::max()) {
    if (FileDescriptor) {
      if (CloseFD)
        _close(FileDescriptor);
    } else
      ::CloseHandle(FileHandle);
    return make_error_code(errc::invalid_argument);
  }

  DWORD flprotect;
  switch (Mode) {
  case readonly:  flprotect = PAGE_READONLY; break;
  case readwrite: flprotect = PAGE_READWRITE; break;
  case priv:      flprotect = PAGE_WRITECOPY; break;
  }

  FileMappingHandle = ::CreateFileMapping(FileHandle,
                                          0,
                                          flprotect,
                                          (Offset + Size) >> 32,
                                          (Offset + Size) & 0xffffffff,
                                          0);
  if (FileMappingHandle == NULL) {
    error_code ec = windows_error(GetLastError());
    if (FileDescriptor) {
      if (CloseFD)
        _close(FileDescriptor);
    } else
      ::CloseHandle(FileHandle);
    return ec;
  }

  DWORD dwDesiredAccess;
  switch (Mode) {
  case readonly:  dwDesiredAccess = FILE_MAP_READ; break;
  case readwrite: dwDesiredAccess = FILE_MAP_WRITE; break;
  case priv:      dwDesiredAccess = FILE_MAP_COPY; break;
  }
  Mapping = ::MapViewOfFile(FileMappingHandle,
                            dwDesiredAccess,
                            Offset >> 32,
                            Offset & 0xffffffff,
                            Size);
  if (Mapping == NULL) {
    error_code ec = windows_error(GetLastError());
    ::CloseHandle(FileMappingHandle);
    if (FileDescriptor) {
      if (CloseFD)
        _close(FileDescriptor);
    } else
      ::CloseHandle(FileHandle);
    return ec;
  }

  if (Size == 0) {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T Result = VirtualQuery(Mapping, &mbi, sizeof(mbi));
    if (Result == 0) {
      error_code ec = windows_error(GetLastError());
      ::UnmapViewOfFile(Mapping);
      ::CloseHandle(FileMappingHandle);
      if (FileDescriptor) {
        if (CloseFD)
          _close(FileDescriptor);
      } else
        ::CloseHandle(FileHandle);
      return ec;
    }
    Size = mbi.RegionSize;
  }

  // Close all the handles except for the view. It will keep the other handles
  // alive.
  ::CloseHandle(FileMappingHandle);
  if (FileDescriptor) {
    if (CloseFD)
      _close(FileDescriptor); // Also closes FileHandle.
  } else
    ::CloseHandle(FileHandle);
  return error_code::success();
}

mapped_file_region::mapped_file_region(const Twine &path,
                                       mapmode mode,
                                       uint64_t length,
                                       uint64_t offset,
                                       error_code &ec)
  : Mode(mode)
  , Size(length)
  , Mapping()
  , FileDescriptor()
  , FileHandle(INVALID_HANDLE_VALUE)
  , FileMappingHandle() {
  SmallString<128> path_storage;
  cSmallVector<wchar_t, 128> path_utf16;

  // Convert path to UTF-16.
  if ((ec = UTF8ToUTF16(path.toStringRef(path_storage), path_utf16)))
    return;

  // Get file handle for creating a file mapping.
  FileHandle = ::CreateFileW(c_str(path_utf16),
                             Mode == readonly ? GENERIC_READ
                                              : GENERIC_READ | GENERIC_WRITE,
                             Mode == readonly ? FILE_SHARE_READ
                                              : 0,
                             0,
                             Mode == readonly ? OPEN_EXISTING
                                              : OPEN_ALWAYS,
                             Mode == readonly ? FILE_ATTRIBUTE_READONLY
                                              : FILE_ATTRIBUTE_NORMAL,
                             0);
  if (FileHandle == INVALID_HANDLE_VALUE) {
    ec = windows_error(::GetLastError());
    return;
  }

  FileDescriptor = 0;
  ec = init(FileDescriptor, true, offset);
  if (ec) {
    Mapping = FileMappingHandle = 0;
    FileHandle = INVALID_HANDLE_VALUE;
    FileDescriptor = 0;
  }
}

mapped_file_region::mapped_file_region(int fd,
                                       bool closefd,
                                       mapmode mode,
                                       uint64_t length,
                                       uint64_t offset,
                                       error_code &ec)
  : Mode(mode)
  , Size(length)
  , Mapping()
  , FileDescriptor(fd)
  , FileHandle(INVALID_HANDLE_VALUE)
  , FileMappingHandle() {
  FileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if (FileHandle == INVALID_HANDLE_VALUE) {
    if (closefd)
      _close(FileDescriptor);
    FileDescriptor = 0;
    ec = make_error_code(errc::bad_file_descriptor);
    return;
  }

  ec = init(FileDescriptor, closefd, offset);
  if (ec) {
    Mapping = FileMappingHandle = 0;
    FileHandle = INVALID_HANDLE_VALUE;
    FileDescriptor = 0;
  }
}

mapped_file_region::~mapped_file_region() {
  if (Mapping)
    ::UnmapViewOfFile(Mapping);
}

#if LLVM_HAS_RVALUE_REFERENCES
mapped_file_region::mapped_file_region(mapped_file_region &&other)
  : Mode(other.Mode)
  , Size(other.Size)
  , Mapping(other.Mapping)
  , FileDescriptor(other.FileDescriptor)
  , FileHandle(other.FileHandle)
  , FileMappingHandle(other.FileMappingHandle) {
  other.Mapping = other.FileMappingHandle = 0;
  other.FileHandle = INVALID_HANDLE_VALUE;
  other.FileDescriptor = 0;
}
#endif

mapped_file_region::mapmode mapped_file_region::flags() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Mode;
}

uint64_t mapped_file_region::size() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Size;
}

char *mapped_file_region::data() const {
  assert(Mode != readonly && "Cannot get non const data for readonly mapping!");
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<char*>(Mapping);
}

const char *mapped_file_region::const_data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<const char*>(Mapping);
}

int mapped_file_region::alignment() {
  SYSTEM_INFO SysInfo;
  ::GetSystemInfo(&SysInfo);
  return SysInfo.dwAllocationGranularity;
}

error_code detail::directory_iterator_construct(detail::DirIterState &it,
                                                cStringRef path){
  cSmallVector<wchar_t, 128> path_utf16;

  if (error_code ec = UTF8ToUTF16(path,
                                  path_utf16))
    return ec;

  // Convert path to the format that Windows is happy with.
  if (path_utf16.size() > 0 &&
      !is_separator(path_utf16[path.size() - 1]) &&
      path_utf16[path.size() - 1] != L':') {
    path_utf16.push_back(L'\\');
    path_utf16.push_back(L'*');
  } else {
    path_utf16.push_back(L'*');
  }

  //  Get the first directory entry.
  WIN32_FIND_DATAW FirstFind;
  ScopedFindHandle FindHandle(::FindFirstFileW(c_str(path_utf16), &FirstFind));
  if (!FindHandle)
    return windows_error(::GetLastError());

  size_t FilenameLen = ::wcslen(FirstFind.cFileName);
  while ((FilenameLen == 1 && FirstFind.cFileName[0] == L'.') ||
         (FilenameLen == 2 && FirstFind.cFileName[0] == L'.' &&
                              FirstFind.cFileName[1] == L'.'))
    if (!::FindNextFileW(FindHandle, &FirstFind)) {
      error_code ec = windows_error(::GetLastError());
      // Check for end.
      if (ec == windows_error::no_more_files)
        return detail::directory_iterator_destruct(it);
      return ec;
    } else
      FilenameLen = ::wcslen(FirstFind.cFileName);

  // Construct the current directory entry.
  SmallString<128> directory_entry_name_utf8;
  if (error_code ec = UTF16ToUTF8(FirstFind.cFileName,
                                  ::wcslen(FirstFind.cFileName),
                                  directory_entry_name_utf8))
    return ec;

  it.IterationHandle = intptr_t(FindHandle.take());
  SmallString<128> directory_entry_path(path);
  path::append(directory_entry_path, directory_entry_name_utf8.str());
  it.CurrentEntry = directory_entry(directory_entry_path.str());

  return error_code::success();
}

error_code detail::directory_iterator_destruct(detail::DirIterState &it) {
  if (it.IterationHandle != 0)
    // Closes the handle if it's valid.
    ScopedFindHandle close(HANDLE(it.IterationHandle));
  it.IterationHandle = 0;
  it.CurrentEntry = directory_entry();
  return error_code::success();
}

error_code detail::directory_iterator_increment(detail::DirIterState &it) {
  WIN32_FIND_DATAW FindData;
  if (!::FindNextFileW(HANDLE(it.IterationHandle), &FindData)) {
    error_code ec = windows_error(::GetLastError());
    // Check for end.
    if (ec == windows_error::no_more_files)
      return detail::directory_iterator_destruct(it);
    return ec;
  }

  size_t FilenameLen = ::wcslen(FindData.cFileName);
  if ((FilenameLen == 1 && FindData.cFileName[0] == L'.') ||
      (FilenameLen == 2 && FindData.cFileName[0] == L'.' &&
                           FindData.cFileName[1] == L'.'))
    return directory_iterator_increment(it);

  SmallString<128> directory_entry_path_utf8;
  if (error_code ec = UTF16ToUTF8(FindData.cFileName,
                                  ::wcslen(FindData.cFileName),
                                  directory_entry_path_utf8))
    return ec;

  it.CurrentEntry.replace_filename(Twine(directory_entry_path_utf8));
  return error_code::success();
}

error_code map_file_pages(const Twine &path, off_t file_offset, size_t size,
                                            bool map_writable, void *&result) {
  assert(0 && "NOT IMPLEMENTED");
  return windows_error::invalid_function;
}

error_code unmap_file_pages(void *base, size_t size) {
  assert(0 && "NOT IMPLEMENTED");
  return windows_error::invalid_function;
}

error_code openFileForRead(const Twine &Name, int &ResultFD) {
  SmallString<128> PathStorage;
  cSmallVector<wchar_t, 128> PathUTF16;

  if (error_code EC = UTF8ToUTF16(Name.toStringRef(PathStorage),
                                  PathUTF16))
    return EC;

  HANDLE H = ::CreateFileW(PathUTF16.begin(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (H == INVALID_HANDLE_VALUE) {
    error_code EC = windows_error(::GetLastError());
    // Provide a better error message when trying to open directories.
    // This only runs if we failed to open the file, so there is probably
    // no performances issues.
    if (EC != windows_error::access_denied)
      return EC;
    if (is_directory(Name))
      return error_code(errc::is_a_directory, posix_category());
    return EC;
  }

  int FD = ::_open_osfhandle(intptr_t(H), 0);
  if (FD == -1) {
    ::CloseHandle(H);
    return windows_error::invalid_handle;
  }

  ResultFD = FD;
  return error_code::success();
}

error_code openFileForWrite(const Twine &Name, int &ResultFD,
                            sys::fs::OpenFlags Flags, unsigned Mode) {
  // Verify that we don't have both "append" and "excl".
  assert((!(Flags & sys::fs::F_Excl) || !(Flags & sys::fs::F_Append)) &&
         "Cannot specify both 'excl' and 'append' file creation flags!");

  SmallString<128> PathStorage;
  cSmallVector<wchar_t, 128> PathUTF16;

  if (error_code EC = UTF8ToUTF16(Name.toStringRef(PathStorage),
                                  PathUTF16))
    return EC;

  DWORD CreationDisposition;
  if (Flags & F_Excl)
    CreationDisposition = CREATE_NEW;
  else if (Flags & F_Append)
    CreationDisposition = OPEN_ALWAYS;
  else
    CreationDisposition = CREATE_ALWAYS;

  HANDLE H = ::CreateFileW(PathUTF16.begin(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           CreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

  if (H == INVALID_HANDLE_VALUE) {
    error_code EC = windows_error(::GetLastError());
    // Provide a better error message when trying to open directories.
    // This only runs if we failed to open the file, so there is probably
    // no performances issues.
    if (EC != windows_error::access_denied)
      return EC;
    if (is_directory(Name))
      return error_code(errc::is_a_directory, posix_category());
    return EC;
  }

  int OpenFlags = 0;
  if (Flags & F_Append)
    OpenFlags |= _O_APPEND;

  if (!(Flags & F_Binary))
    OpenFlags |= _O_TEXT;

  int FD = ::_open_osfhandle(intptr_t(H), OpenFlags);
  if (FD == -1) {
    ::CloseHandle(H);
    return windows_error::invalid_handle;
  }

  ResultFD = FD;
  return error_code::success();
}
} // end namespace fs

namespace windows {
akj::error_code UTF8ToUTF16(akj::cStringRef utf8,
                             akj::cSmallVectorImpl<wchar_t> &utf16) {
  int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  utf8.begin(), utf8.size(),
                                  utf16.begin(), 0);

  if (len == 0)
    return akj::windows_error(::GetLastError());

  utf16.reserve(len + 1);
  utf16.set_size(len);

  len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                              utf8.begin(), utf8.size(),
                              utf16.begin(), utf16.size());

  if (len == 0)
    return akj::windows_error(::GetLastError());

  // Make utf16 null terminated.
  utf16.push_back(0);
  utf16.pop_back();

  return akj::error_code::success();
}

akj::error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                             akj::cSmallVectorImpl<char> &utf8) {
  // Get length.
  int len = ::WideCharToMultiByte(CP_UTF8, 0,
                                  utf16, utf16_len,
                                  utf8.begin(), 0,
                                  NULL, NULL);

  if (len == 0)
    return akj::windows_error(::GetLastError());

  utf8.reserve(len);
  utf8.set_size(len);

  // Now do the actual conversion.
  len = ::WideCharToMultiByte(CP_UTF8, 0,
                              utf16, utf16_len,
                              utf8.data(), utf8.size(),
                              NULL, NULL);

  if (len == 0)
    return akj::windows_error(::GetLastError());

  // Make utf8 null terminated.
  utf8.push_back(0);
  utf8.pop_back();

  return akj::error_code::success();
}
} // end namespace windows
} // end namespace sys
} // end namespace akj



#else

#ifndef AKJ_DEFINED_UNIX_FS
#define AKJ_DEFINED_UNIX_FS

#include "SystemSpecific/Unix.hpp"

#include "ProcessUtils.hpp"
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
//#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#if AKJ_IS_THIS_NEEDED
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Both stdio.h and cstdio are included via different pathes and
// stdcxx's cstdio doesn't include stdio.h, so it doesn't #undef the macros
// either.
#undef ferror
#undef feof

// For GNU Hurd
#if defined(__GNU__) && !defined(PATH_MAX)
# define PATH_MAX 4096
#endif

#define MAXPATHLEN 160


using namespace akj;

namespace {
  /// This class automatically closes the given file descriptor when it goes out
  /// of scope. You can take back explicit ownership of the file descriptor by
  /// calling take(). The destructor does not verify that close was successful.
  /// Therefore, never allow this class to call close on a file descriptor that
  /// has been read from or written to.
  struct AutoFD {
    int FileDescriptor;

    AutoFD(int fd) : FileDescriptor(fd) {}
    ~AutoFD() {
      if (FileDescriptor >= 0)
        ::close(FileDescriptor);
    }

    int take() {
      int ret = FileDescriptor;
      FileDescriptor = -1;
      return ret;
    }

    operator int() const {return FileDescriptor;}
  };

  error_code TempDir(cSmallVectorImpl<char> &result) {
    // FIXME: Don't use TMPDIR if program is SUID or SGID enabled.
    const char *dir = 0;
    (dir = std::getenv("TMPDIR" )) ||
    (dir = std::getenv("TMP"    )) ||
    (dir = std::getenv("TEMP"   )) ||
    (dir = std::getenv("TEMPDIR")) ||
#ifdef P_tmpdir
    (dir = P_tmpdir) ||
#endif
    (dir = "/tmp");

    result.clear();
    cStringRef d(dir);
    result.append(d.begin(), d.end());
    return error_code::success();
  }
}

static error_code createUniqueEntity(const Twine &Model, int &ResultFD,
                                     cSmallVectorImpl<char> &ResultPath,
                                     bool MakeAbsolute, unsigned Mode,
                                     FSEntity Type) {
  SmallString<128> ModelStorage;
  Model.toVector(ModelStorage);

  if (MakeAbsolute) {
    // Make model absolute by prepending a temp directory if it's not already.
    bool absolute = sys::path::is_absolute(Twine(ModelStorage));
    if (!absolute) {
      SmallString<128> TDir;
      if (error_code ec = TempDir(TDir)) return ec;
      sys::path::append(TDir, Twine(ModelStorage));
      ModelStorage.swap(TDir);
    }
  }

  // From here on, DO NOT modify model. It may be needed if the randomly chosen
  // path already exists.
  ResultPath = ModelStorage;
  // Null terminate.
  ResultPath.push_back(0);
  ResultPath.pop_back();

retry_random_path:
  // Replace '%' with random chars.
  for (unsigned i = 0, e = ModelStorage.size(); i != e; ++i) {
    if (ModelStorage[i] == '%')
      ResultPath[i] = "0123456789abcdef"[sys::Process::GetRandomNumber() & 15];
  }

  // Try to open + create the file.
  switch (Type) {
  case FS_File: {
    int RandomFD = ::open(ResultPath.begin(), O_RDWR | O_CREAT | O_EXCL, Mode);
    if (RandomFD == -1) {
      int SavedErrno = errno;
      // If the file existed, try again, otherwise, error.
      if (SavedErrno == errc::file_exists)
        goto retry_random_path;
      return error_code(SavedErrno, system_category());
    }

    ResultFD = RandomFD;
    return error_code::success();
  }

  case FS_Name: {
    bool Exists;
    error_code EC = sys::fs::exists(ResultPath.begin(), Exists);
    if (EC)
      return EC;
    if (Exists)
      goto retry_random_path;
    return error_code::success();
  }

  case FS_Dir: {
    bool Existed;
    error_code EC = sys::fs::create_directory(ResultPath.begin(), Existed);
    if (EC)
      return EC;
    if (Existed)
      goto retry_random_path;
    return error_code::success();
  }
  }
  FatalError::Die("Invalid Type");
}

namespace akj {
namespace sys  {
namespace fs {
#if defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
    defined(__OpenBSD__) || defined(__minix) || defined(__FreeBSD_kernel__) || \
    defined(__linux__) || defined(__CYGWIN__)
static int
test_dir(char buf[PATH_MAX], char ret[PATH_MAX],
    const char *dir, const char *bin)
{
  struct stat sb;

  snprintf(buf, PATH_MAX, "%s/%s", dir, bin);
  if (realpath(buf, ret) == NULL)
    return (1);
  if (stat(buf, &sb) != 0)
    return (1);

  return (0);
}

static char *
getprogpath(char ret[PATH_MAX], const char *bin)
{
  char *pv, *s, *t, buf[PATH_MAX];

  /* First approach: absolute path. */
  if (bin[0] == '/') {
    if (test_dir(buf, ret, "/", bin) == 0)
      return (ret);
    return (NULL);
  }

  /* Second approach: relative path. */
  if (strchr(bin, '/') != NULL) {
    if (getcwd(buf, PATH_MAX) == NULL)
      return (NULL);
    if (test_dir(buf, ret, buf, bin) == 0)
      return (ret);
    return (NULL);
  }

  /* Third approach: $PATH */
  if ((pv = getenv("PATH")) == NULL)
    return (NULL);
  s = pv = strdup(pv);
  if (pv == NULL)
    return (NULL);
  while ((t = strsep(&s, ":")) != NULL) {
    if (test_dir(buf, ret, t, bin) == 0) {
      free(pv);
      return (ret);
    }
  }
  free(pv);
  return (NULL);
}
#endif // __FreeBSD__ || __NetBSD__ || __FreeBSD_kernel__

/// GetMainExecutable - Return the path to the main executable, given the
/// value of argv[0] from program startup.
std::string getMainExecutable(const char *argv0, void *MainAddr) {
#if defined(__APPLE__)
  // On OS X the executable path is saved to the stack by dyld. Reading it
  // from there is much faster than calling dladdr, especially for large
  // binaries with symbols.
  char exe_path[MAXPATHLEN];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) == 0) {
    char link_path[MAXPATHLEN];
    if (realpath(exe_path, link_path))
      return link_path;
  }
#elif defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
      defined(__OpenBSD__) || defined(__minix) || defined(__FreeBSD_kernel__)
  char exe_path[PATH_MAX];

  if (getprogpath(exe_path, argv0) != NULL)
    return exe_path;
#elif defined(__linux__) || defined(__CYGWIN__)
  char exe_path[MAXPATHLEN];
  cStringRef aPath("/proc/self/exe");
  if (sys::fs::exists(aPath)) {
      // /proc is not always mounted under Linux (chroot for example).
      ssize_t len = readlink(aPath.str().c_str(), exe_path, sizeof(exe_path));
      if (len >= 0)
          return cStringRef(exe_path, len);
  } else {
      // Fall back to the classical detection.
      if (getprogpath(exe_path, argv0) != NULL)
          return exe_path;
  }
#elif defined(HAVE_DLFCN_H)
  // Use dladdr to get executable path if available.
  Dl_info DLInfo;
  int err = dladdr(MainAddr, &DLInfo);
  if (err == 0)
    return "";

  // If the filename is a symlink, we need to resolve and return the location of
  // the actual executable.
  char link_path[MAXPATHLEN];
  if (realpath(DLInfo.dli_fname, link_path))
    return link_path;
#else
#error GetMainExecutable is not implemented on this host yet.
#endif
  return "";
}

TimeValue file_status::getLastModificationTime() const {
  TimeValue Ret;
  Ret.fromEpochTime(fs_st_mtime);
  return Ret;
}

UniqueID file_status::getUniqueID() const {
  return UniqueID(fs_st_dev, fs_st_ino);
}

error_code current_path(cSmallVectorImpl<char> &result) {
  result.clear();

  const char *pwd = ::getenv("PWD");
  akj::sys::fs::file_status PWDStatus, DotStatus;
  if (pwd && akj::sys::path::is_absolute(pwd) &&
      !akj::sys::fs::status(pwd, PWDStatus) &&
      !akj::sys::fs::status(".", DotStatus) &&
      PWDStatus.getUniqueID() == DotStatus.getUniqueID()) {
    result.append(pwd, pwd + strlen(pwd));
    return error_code::success();
  }

#ifdef MAXPATHLEN
  result.reserve(MAXPATHLEN);
#else
// For GNU Hurd
  result.reserve(1024);
#endif

  while (true) {
    if (::getcwd(result.data(), result.capacity()) == 0) {
      // See if there was a real error.
      if (errno != errc::not_enough_memory)
        return error_code(errno, system_category());
      // Otherwise there just wasn't enough space.
      result.reserve(result.capacity() * 2);
    } else
      break;
  }

  result.set_size(strlen(result.data()));
  return error_code::success();
}

error_code create_directory(const Twine &path, bool &existed) {
  SmallString<128> path_storage;
  cStringRef p = path.toNullTerminatedStringRef(path_storage);

  if (::mkdir(p.begin(), S_IRWXU | S_IRWXG) == -1) {
    if (errno != errc::file_exists)
      return error_code(errno, system_category());
    existed = true;
  } else
    existed = false;

  return error_code::success();
}

error_code create_hard_link(const Twine &to, const Twine &from) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toNullTerminatedStringRef(from_storage);
  cStringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::link(t.begin(), f.begin()) == -1)
    return error_code(errno, system_category());

  return error_code::success();
}

error_code create_symlink(const Twine &to, const Twine &from) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toNullTerminatedStringRef(from_storage);
  cStringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::symlink(t.begin(), f.begin()) == -1)
    return error_code(errno, system_category());

  return error_code::success();
}

error_code remove(const Twine &path, bool &existed) {
  SmallString<128> path_storage;
  cStringRef p = path.toNullTerminatedStringRef(path_storage);

  struct stat buf;
  if (stat(p.begin(), &buf) != 0) {
    if (errno != errc::no_such_file_or_directory)
      return error_code(errno, system_category());
    existed = false;
    return error_code::success();
  }

  // Note: this check catches strange situations. In all cases, LLVM should
  // only be involved in the creation and deletion of regular files.  This
  // check ensures that what we're trying to erase is a regular file. It
  // effectively prevents LLVM from erasing things like /dev/null, any block
  // special file, or other things that aren't "regular" files.
  if (!S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode))
    return make_error_code(errc::operation_not_permitted);

  if (::remove(p.begin()) == -1) {
    if (errno != errc::no_such_file_or_directory)
      return error_code(errno, system_category());
    existed = false;
  } else
    existed = true;

  return error_code::success();
}

error_code rename(const Twine &from, const Twine &to) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  cStringRef f = from.toNullTerminatedStringRef(from_storage);
  cStringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::rename(f.begin(), t.begin()) == -1)
    return error_code(errno, system_category());

  return error_code::success();
}

error_code resize_file(const Twine &path, uint64_t size) {
  SmallString<128> path_storage;
  cStringRef p = path.toNullTerminatedStringRef(path_storage);

  if (::truncate(p.begin(), size) == -1)
    return error_code(errno, system_category());

  return error_code::success();
}

error_code exists(const Twine &path, bool &result) {
  SmallString<128> path_storage;
  cStringRef p = path.toNullTerminatedStringRef(path_storage);

  if (::access(p.begin(), F_OK) == -1) {
    if (errno != errc::no_such_file_or_directory)
      return error_code(errno, system_category());
    result = false;
  } else
    result = true;

  return error_code::success();
}

bool can_write(const Twine &Path) {
  SmallString<128> PathStorage;
  cStringRef P = Path.toNullTerminatedStringRef(PathStorage);
  return 0 == access(P.begin(), W_OK);
}

bool can_execute(const Twine &Path) {
  SmallString<128> PathStorage;
  cStringRef P = Path.toNullTerminatedStringRef(PathStorage);

  if (0 != access(P.begin(), R_OK | X_OK))
    return false;
  struct stat buf;
  if (0 != stat(P.begin(), &buf))
    return false;
  if (!S_ISREG(buf.st_mode))
    return false;
  return true;
}

bool equivalent(file_status A, file_status B) {
  assert(status_known(A) && status_known(B));
  return A.fs_st_dev == B.fs_st_dev &&
         A.fs_st_ino == B.fs_st_ino;
}

error_code equivalent(const Twine &A, const Twine &B, bool &result) {
  file_status fsA, fsB;
  if (error_code ec = status(A, fsA)) return ec;
  if (error_code ec = status(B, fsB)) return ec;
  result = equivalent(fsA, fsB);
  return error_code::success();
}

static error_code fillStatus(int StatRet, const struct stat &Status,
                             file_status &Result) {
  if (StatRet != 0) {
    error_code ec(errno, system_category());
    if (ec == errc::no_such_file_or_directory)
      Result = file_status(file_type::file_not_found);
    else
      Result = file_status(file_type::status_error);
    return ec;
  }

  file_type Type = file_type::type_unknown;

  if (S_ISDIR(Status.st_mode))
    Type = file_type::directory_file;
  else if (S_ISREG(Status.st_mode))
    Type = file_type::regular_file;
  else if (S_ISBLK(Status.st_mode))
    Type = file_type::block_file;
  else if (S_ISCHR(Status.st_mode))
    Type = file_type::character_file;
  else if (S_ISFIFO(Status.st_mode))
    Type = file_type::fifo_file;
  else if (S_ISSOCK(Status.st_mode))
    Type = file_type::socket_file;

  perms Perms = static_cast<perms>(Status.st_mode);
  Result =
      file_status(Type, Perms, Status.st_dev, Status.st_ino, Status.st_mtime,
                  Status.st_uid, Status.st_gid, Status.st_size);

  return error_code::success();
}

error_code status(const Twine &Path, file_status &Result) {
  SmallString<128> PathStorage;
  cStringRef P = Path.toNullTerminatedStringRef(PathStorage);

  struct stat Status;
  int StatRet = ::stat(P.begin(), &Status);
  return fillStatus(StatRet, Status, Result);
}

error_code status(int FD, file_status &Result) {
  struct stat Status;
  int StatRet = ::fstat(FD, &Status);
  return fillStatus(StatRet, Status, Result);
}

error_code setLastModificationAndAccessTime(int FD, TimeValue Time) {

  timespec Times[2];
  Times[0].tv_sec = Time.toPosixTime();
  Times[0].tv_nsec = 0;
  Times[1] = Times[0];
  if (::futimens(FD, Times))
    {return error_code(errno, system_category());}
  return error_code::success();
}

error_code mapped_file_region::init(int FD, bool CloseFD, uint64_t Offset) {
  AutoFD ScopedFD(FD);
  if (!CloseFD)
    ScopedFD.take();

  // Figure out how large the file is.
  struct stat FileInfo;
  if (fstat(FD, &FileInfo) == -1)
    return error_code(errno, system_category());
  uint64_t FileSize = FileInfo.st_size;

  if (Size == 0)
    Size = FileSize;
  else if (FileSize < Size) {
    // We need to grow the file.
    if (ftruncate(FD, Size) == -1)
      return error_code(errno, system_category());
  }

  int flags = (Mode == readwrite) ? MAP_SHARED : MAP_PRIVATE;
  int prot = (Mode == readonly) ? PROT_READ : (PROT_READ | PROT_WRITE);
  flags |= MAP_FILE;
  Mapping = ::mmap(0, Size, prot, flags, FD, Offset);
  if (Mapping == MAP_FAILED)
    return error_code(errno, system_category());
  return error_code::success();
}

mapped_file_region::mapped_file_region(const Twine &path,
                                       mapmode mode,
                                       uint64_t length,
                                       uint64_t offset,
                                       error_code &ec)
  : Mode(mode)
  , Size(length)
  , Mapping() {
  // Make sure that the requested size fits within SIZE_T.
  if (length > std::numeric_limits<size_t>::max()) {
    ec = make_error_code(errc::invalid_argument);
    return;
  }

  SmallString<128> path_storage;
  cStringRef name = path.toNullTerminatedStringRef(path_storage);
  int oflags = (mode == readonly) ? O_RDONLY : O_RDWR;
  int ofd = ::open(name.begin(), oflags);
  if (ofd == -1) {
    ec = error_code(errno, system_category());
    return;
  }

  ec = init(ofd, true, offset);
  if (ec)
    Mapping = 0;
}

mapped_file_region::mapped_file_region(int fd,
                                       bool closefd,
                                       mapmode mode,
                                       uint64_t length,
                                       uint64_t offset,
                                       error_code &ec)
  : Mode(mode)
  , Size(length)
  , Mapping() {
  // Make sure that the requested size fits within SIZE_T.
  if (length > std::numeric_limits<size_t>::max()) {
    ec = make_error_code(errc::invalid_argument);
    return;
  }

  ec = init(fd, closefd, offset);
  if (ec)
    Mapping = 0;
}

mapped_file_region::~mapped_file_region() {
  if (Mapping)
    ::munmap(Mapping, Size);
}

#if LLVM_HAS_RVALUE_REFERENCES
mapped_file_region::mapped_file_region(mapped_file_region &&other)
  : Mode(other.Mode), Size(other.Size), Mapping(other.Mapping) {
  other.Mapping = 0;
}
#endif

mapped_file_region::mapmode mapped_file_region::flags() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Mode;
}

uint64_t mapped_file_region::size() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Size;
}

char *mapped_file_region::data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  assert(Mode != readonly && "Cannot get non const data for readonly mapping!");
  return reinterpret_cast<char*>(Mapping);
}

const char *mapped_file_region::const_data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<const char*>(Mapping);
}

int mapped_file_region::alignment() {
  return process::get_self()->page_size();
}

error_code detail::directory_iterator_construct(detail::DirIterState &it,
                                                cStringRef path){
  SmallString<128> path_null(path);
  DIR *directory = ::opendir(path_null.c_str());
  if (directory == 0)
    return error_code(errno, system_category());

  it.IterationHandle = reinterpret_cast<intptr_t>(directory);
  // Add something for replace_filename to replace.
  path::append(path_null, ".");
  it.CurrentEntry = directory_entry(path_null.str());
  return directory_iterator_increment(it);
}

error_code detail::directory_iterator_destruct(detail::DirIterState &it) {
  if (it.IterationHandle)
    ::closedir(reinterpret_cast<DIR *>(it.IterationHandle));
  it.IterationHandle = 0;
  it.CurrentEntry = directory_entry();
  return error_code::success();
}

error_code detail::directory_iterator_increment(detail::DirIterState &it) {
  errno = 0;
  dirent *cur_dir = ::readdir(reinterpret_cast<DIR *>(it.IterationHandle));
  if (cur_dir == 0 && errno != 0) {
    return error_code(errno, system_category());
  } else if (cur_dir != 0) {
    cStringRef name(cur_dir->d_name, NAMLEN(cur_dir));
    if ((name.size() == 1 && name[0] == '.') ||
        (name.size() == 2 && name[0] == '.' && name[1] == '.'))
      return directory_iterator_increment(it);
    it.CurrentEntry.replace_filename(name);
  } else
    return directory_iterator_destruct(it);

  return error_code::success();
}

error_code get_magic(const Twine &path, uint32_t len,
                     cSmallVectorImpl<char> &result) {
  SmallString<128> PathStorage;
  cStringRef Path = path.toNullTerminatedStringRef(PathStorage);
  result.set_size(0);

  // Open path.
  std::FILE *file = std::fopen(Path.data(), "rb");
  if (file == 0)
    return error_code(errno, system_category());

  // Reserve storage.
  result.reserve(len);

  // Read magic!
  size_t size = std::fread(result.data(), 1, len, file);
  if (std::ferror(file) != 0) {
    std::fclose(file);
    return error_code(errno, system_category());
  } else if (size != len) {
    if (std::feof(file) != 0) {
      std::fclose(file);
      result.set_size(size);
      return make_error_code(errc::value_too_large);
    }
  }
  std::fclose(file);
  result.set_size(size);
  return error_code::success();
}

error_code map_file_pages(const Twine &path, off_t file_offset, size_t size,  
                                            bool map_writable, void *&result) {
  SmallString<128> path_storage;
  cStringRef name = path.toNullTerminatedStringRef(path_storage);
  int oflags = map_writable ? O_RDWR : O_RDONLY;
  int ofd = ::open(name.begin(), oflags);
  if ( ofd == -1 )
    return error_code(errno, system_category());
  AutoFD fd(ofd);
  int flags = map_writable ? MAP_SHARED : MAP_PRIVATE;
  int prot = map_writable ? (PROT_READ|PROT_WRITE) : PROT_READ;
#ifdef MAP_FILE
  flags |= MAP_FILE;
#endif
  result = ::mmap(0, size, prot, flags, fd, file_offset);
  if (result == MAP_FAILED) {
    return error_code(errno, system_category());
  }
  
  return error_code::success();
}

error_code unmap_file_pages(void *base, size_t size) {
  if ( ::munmap(base, size) == -1 )
    return error_code(errno, system_category());
   
  return error_code::success();
}

error_code openFileForRead(const Twine &Name, int &ResultFD) {
  SmallString<128> Storage;
  cStringRef P = Name.toNullTerminatedStringRef(Storage);
  while ((ResultFD = open(P.begin(), O_RDONLY)) < 0) {
    if (errno != EINTR)
      return error_code(errno, system_category());
  }
  return error_code::success();
}

error_code openFileForWrite(const Twine &Name, int &ResultFD,
                            sys::fs::OpenFlags Flags, unsigned Mode) {
  // Verify that we don't have both "append" and "excl".
  assert((!(Flags & sys::fs::F_Excl) || !(Flags & sys::fs::F_Append)) &&
         "Cannot specify both 'excl' and 'append' file creation flags!");

  int OpenFlags = O_WRONLY | O_CREAT;

  if (Flags & F_Append)
    OpenFlags |= O_APPEND;
  else
    OpenFlags |= O_TRUNC;

  if (Flags & F_Excl)
    OpenFlags |= O_EXCL;

  SmallString<128> Storage;
  cStringRef P = Name.toNullTerminatedStringRef(Storage);
  while ((ResultFD = open(P.begin(), OpenFlags, Mode)) < 0) {
    if (errno != EINTR)
      return error_code(errno, system_category());
  }
  return error_code::success();
}

} // end namespace fs
} // end namespace sys
} // end namespace akj


#endif //include guard 
#endif
