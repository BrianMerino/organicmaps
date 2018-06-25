#include "platform/platform.hpp"

#include "platform/local_country_file.hpp"

#include "coding/base64.hpp"
#include "coding/file_name_utils.hpp"
#include "coding/internal/file_data.hpp"
#include "coding/writer.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <thread>

#include "std/target_os.hpp"

#include "private.h"

#include <errno.h>

using namespace std;

namespace
{
bool IsSpecialDirName(string const & dirName)
{
  return dirName == "." || dirName == "..";
}

bool GetFileTypeChecked(string const & path, Platform::EFileType & type)
{
  Platform::EError const ret = Platform::GetFileType(path, type);
  if (ret != Platform::ERR_OK)
  {
    LOG(LERROR, ("Can't determine file type for", path, ":", ret));
    return false;
  }
  return true;
}
} // namespace

// static
Platform::EError Platform::ErrnoToError()
{
  switch (errno)
  {
    case ENOENT:
      return ERR_FILE_DOES_NOT_EXIST;
    case EACCES:
      return ERR_ACCESS_FAILED;
    case ENOTEMPTY:
      return ERR_DIRECTORY_NOT_EMPTY;
    case EEXIST:
      return ERR_FILE_ALREADY_EXISTS;
    case ENAMETOOLONG:
      return ERR_NAME_TOO_LONG;
    case ENOTDIR:
      return ERR_NOT_A_DIRECTORY;
    case ELOOP:
      return ERR_SYMLINK_LOOP;
    case EIO:
      return ERR_IO_ERROR;
    default:
      return ERR_UNKNOWN;
  }
}

// static
bool Platform::RmDirRecursively(string const & dirName)
{
  if (dirName.empty() || IsSpecialDirName(dirName))
    return false;

  bool res = true;

  FilesList allFiles;
  GetFilesByRegExp(dirName, ".*", allFiles);
  for (string const & file : allFiles)
  {
    string const path = my::JoinFoldersToPath(dirName, file);

    EFileType type;
    if (GetFileType(path, type) != ERR_OK)
      continue;

    if (type == FILE_TYPE_DIRECTORY)
    {
      if (!IsSpecialDirName(file) && !RmDirRecursively(path))
        res = false;
    }
    else
    {
      if (!my::DeleteFileX(path))
        res = false;
    }
  }

  if (RmDir(dirName) != ERR_OK)
    res = false;

  return res;
}

void Platform::SetSettingsDir(string const & path)
{
  m_settingsDir = my::AddSlashIfNeeded(path);
}

string Platform::ReadPathForFile(string const & file, string searchScope) const
{
  if (searchScope.empty())
    searchScope = "wrf";

  string fullPath;
  for (size_t i = 0; i < searchScope.size(); ++i)
  {
    switch (searchScope[i])
    {
    case 'w': fullPath = m_writableDir + file; break;
    case 'r': fullPath = m_resourcesDir + file; break;
    case 's': fullPath = m_settingsDir + file; break;
    case 'f': fullPath = file; break;
    default : CHECK(false, ("Unsupported searchScope:", searchScope)); break;
    }
    if (IsFileExistsByFullPath(fullPath))
      return fullPath;
  }

  string const possiblePaths = m_writableDir  + "\n" + m_resourcesDir + "\n" + m_settingsDir;
  MYTHROW(FileAbsentException, ("File", file, "doesn't exist in the scope", searchScope,
                                "Have been looking in:\n", possiblePaths));
}

string Platform::ResourcesMetaServerUrl() const
{
  return RESOURCES_METASERVER_URL;
}

string Platform::MetaServerUrl() const
{
  return METASERVER_URL;
}

string Platform::DefaultUrlsJSON() const
{
  return DEFAULT_URLS_JSON;
}

void Platform::GetFontNames(FilesList & res) const
{
  ASSERT(res.empty(), ());

  /// @todo Actually, this list should present once in all our code.
  /// We can take it from data/external_resources.txt
  char const * arrDef[] = {
    "01_dejavusans.ttf",
    "02_droidsans-fallback.ttf",
    "03_jomolhari-id-a3d.ttf",
    "04_padauk.ttf",
    "05_khmeros.ttf",
    "06_code2000.ttf",
    "07_roboto_medium.ttf"
  };
  res.insert(res.end(), arrDef, arrDef + ARRAY_SIZE(arrDef));

  GetSystemFontNames(res);

  LOG(LINFO, ("Available font files:", (res)));
}

void Platform::GetFilesByExt(string const & directory, string const & ext, FilesList & outFiles)
{
  // Transform extension mask to regexp (.mwm -> \.mwm$)
  ASSERT ( !ext.empty(), () );
  ASSERT_EQUAL ( ext[0], '.' , () );

  GetFilesByRegExp(directory, '\\' + ext + '$', outFiles);
}

// static
void Platform::GetFilesByType(string const & directory, unsigned typeMask,
                              TFilesWithType & outFiles)
{
  FilesList allFiles;
  GetFilesByRegExp(directory, ".*", allFiles);
  for (string const & file : allFiles)
  {
    EFileType type;
    if (GetFileType(my::JoinFoldersToPath(directory, file), type) != ERR_OK)
      continue;
    if (typeMask & type)
      outFiles.emplace_back(file, type);
  }
}

// static
bool Platform::IsDirectory(string const & path)
{
  EFileType fileType;
  if (GetFileType(path, fileType) != ERR_OK)
    return false;
  return fileType == FILE_TYPE_DIRECTORY;
}

// static
void Platform::GetFilesRecursively(string const & directory, FilesList & filesList)
{
  TFilesWithType files;

  GetFilesByType(directory, Platform::FILE_TYPE_REGULAR, files);
  for (auto const & p : files)
  {
    auto const & file = p.first;
    CHECK_EQUAL(p.second, Platform::FILE_TYPE_REGULAR, ("dir:", directory, "file:", file));
    filesList.push_back(my::JoinPath(directory, file));
  }

  TFilesWithType subdirs;
  GetFilesByType(directory, Platform::FILE_TYPE_DIRECTORY, subdirs);

  for (auto const & p : subdirs)
  {
    auto const & subdir = p.first;
    CHECK_EQUAL(p.second, Platform::FILE_TYPE_DIRECTORY, ("dir:", directory, "subdir:", subdir));
    if (subdir == "." || subdir == "..")
      continue;

    GetFilesRecursively(my::JoinPath(directory, subdir), filesList);
  }
}

void Platform::SetWritableDirForTests(string const & path)
{
  m_writableDir = my::AddSlashIfNeeded(path);
}

void Platform::SetResourceDir(string const & path)
{
  m_resourcesDir = my::AddSlashIfNeeded(path);
}

// static
bool Platform::MkDirChecked(string const & dirName)
{
  Platform::EError const ret = MkDir(dirName);
  switch (ret)
  {
  case Platform::ERR_OK: return true;
  case Platform::ERR_FILE_ALREADY_EXISTS:
  {
    Platform::EFileType type;
    if (!GetFileTypeChecked(dirName, type))
      return false;
    if (type != Platform::FILE_TYPE_DIRECTORY)
    {
      LOG(LERROR, (dirName, "exists, but not a dirName:", type));
      return false;
    }
    return true;
  }
  default: LOG(LERROR, (dirName, "can't be created:", ret)); return false;
  }
}

unsigned Platform::CpuCores() const
{
  unsigned const cores = thread::hardware_concurrency();
  return cores > 0 ? cores : 1;
}

void Platform::ShutdownThreads()
{
  ASSERT(m_networkThread && m_fileThread && m_backgroundThread, ());
  m_networkThread->ShutdownAndJoin();
  m_fileThread->ShutdownAndJoin();
  m_backgroundThread->ShutdownAndJoin();

  m_networkThread.reset();
  m_fileThread.reset();
  m_backgroundThread.reset();
}

void Platform::RunThreads()
{
  ASSERT(!m_networkThread && !m_fileThread && !m_backgroundThread, ());
  m_networkThread = make_unique<base::WorkerThread>();
  m_fileThread = make_unique<base::WorkerThread>();
  m_backgroundThread = make_unique<base::WorkerThread>();
}

string DebugPrint(Platform::EError err)
{
  switch (err)
  {
  case Platform::ERR_OK: return "Ok";
  case Platform::ERR_FILE_DOES_NOT_EXIST: return "File does not exist.";
  case Platform::ERR_ACCESS_FAILED: return "Access failed.";
  case Platform::ERR_DIRECTORY_NOT_EMPTY: return "Directory not empty.";
  case Platform::ERR_FILE_ALREADY_EXISTS: return "File already exists.";
  case Platform::ERR_NAME_TOO_LONG:
    return "The length of a component of path exceeds {NAME_MAX} characters.";
  case Platform::ERR_NOT_A_DIRECTORY:
    return "A component of the path prefix of Path is not a directory.";
  case Platform::ERR_SYMLINK_LOOP:
    return "Too many symbolic links were encountered in translating path.";
  case Platform::ERR_IO_ERROR: return "An I/O error occurred.";
  case Platform::ERR_UNKNOWN: return "Unknown";
  }
  CHECK_SWITCH();
}

string DebugPrint(Platform::ChargingStatus status)
{
  switch (status)
  {
  case Platform::ChargingStatus::Unknown: return "Unknown";
  case Platform::ChargingStatus::Plugged: return "Plugged";
  case Platform::ChargingStatus::Unplugged: return "Unplugged";
  }
  CHECK_SWITCH();
}
