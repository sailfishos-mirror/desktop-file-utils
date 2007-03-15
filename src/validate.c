/* validate.c: validate a desktop entry file
 * 
 * Copyright (C) 2007 Vincent Untz <vuntz@gnome.org>
 *
 * A really small portion of this code comes from the old validate.c.
 * Authors of the old validate.c are:
 *  Mark McLoughlin
 *  Havoc Pennington
 *  Ray Strode
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <stdio.h>
#include <string.h>

#include "keyfileutils.h"
#include "validate.h"

/* We're trying to not use GKeyFile APIs as much as possible, so we don't
 * have to "trust" what GKeyFile is doing, and GKeyFile can be less
 * strict than this validator.
 * FIXME: maybe it's a good idea to just not use GKeyFile and do all the
 * parsing ourselves. This way, we know we're doing exactly what we want.
 */

//FIXME: document where GKeyFile is stricter than the spec
// * only UTF-8 (so no Legacy-Mixed encoding)
// * "Historically lists have been comma separated."
// Add this information to --usage

/*TODO:
 * + Desktop entry files are encoded as lines of 8-bit characters separated by
 *   LF characters.
 * + Multiple groups may not have the same name.
 * + Lecagy-Mixed Encoding (annexe D)
 * + The escape sequences \s, \n, \t, \r, and \\ are supported for values of
 *   type string and localestring, meaning ASCII space, newline, tab, carriage
 *   return, and backslash, respectively.
 *   GKeyFile handles this, but we don't.
 */

typedef enum {
  INVALID_TYPE = 0,

  APPLICATION_TYPE,
  LINK_TYPE,
  DIRECTORY_TYPE,

  /* Types reserved for KDE */
  /* since 0.9.4 */
  SERVICE_TYPE,
  SERVICE_TYPE_TYPE,
  /* since 0.9.6 */
  FSDEVICE_TYPE,

  /* Deprecated types */
  /* since 0.9.4 */
  MIMETYPE_TYPE,

  LAST_TYPE
} DesktopType;

typedef enum {
  DESKTOP_STRING_TYPE,
  DESKTOP_LOCALESTRING_TYPE,
  DESKTOP_BOOLEAN_TYPE,
  DESKTOP_NUMERIC_TYPE,
  DESKTOP_STRING_LIST_TYPE,
  /* Deprecated types */
  /* since 0.9.6 */
  DESKTOP_REGEXP_LIST_TYPE
} DesktopKeyType;

typedef struct _kf_validator kf_validator;

struct _kf_validator {
  const char  *filename;
  GKeyFile    *keyfile;

  gboolean     kde_reserved_warnings;
  gboolean     no_deprecated_warnings;

  char        *main_group;
  DesktopType  type;
  char        *type_string;

  gboolean     show_in;
  GList       *application_keys;
  GList       *link_keys;
  GList       *fsdevice_keys;
  GList       *mimetype_keys;

  GHashTable  *action_values;
  GHashTable  *action_groups;

  gboolean     fatal_error;
};

static gboolean
validate_string_key (kf_validator *kf,
                     const char   *key,
                     const char   *locale,
                     const char   *value);
static gboolean
validate_localestring_key (kf_validator *kf,
                           const char   *key,
                           const char   *locale,
                           const char   *value);
static gboolean
validate_boolean_key (kf_validator *kf,
                      const char   *key,
                      const char   *locale,
                      const char   *value);
static gboolean
validate_numeric_key (kf_validator *kf,
                      const char   *key,
                      const char   *locale,
                      const char   *value);
static gboolean
validate_string_list_key (kf_validator *kf,
                          const char   *key,
                          const char   *locale,
                          const char   *value);
static gboolean
validate_regexp_list_key (kf_validator *kf,
                          const char   *key,
                          const char   *locale,
                          const char   *value);

static gboolean
handle_type_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value);
static gboolean
handle_version_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value);
static gboolean
handle_comment_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value);
static gboolean
handle_show_in_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value);
static gboolean
handle_exec_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value);
static gboolean
handle_path_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value);
static gboolean
handle_mime_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value);
static gboolean
handle_categories_key (kf_validator *kf,
                       const char   *locale_key,
                       const char   *value);
static gboolean
handle_actions_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value);
static gboolean
handle_dev_key (kf_validator *kf,
                const char   *locale_key,
                const char   *value);
static gboolean
handle_mountpoint_key (kf_validator *kf,
                       const char   *locale_key,
                       const char   *value);
static gboolean
handle_encoding_key (kf_validator *kf,
                     const char   *locale_key,
                     const char   *value);
static gboolean
handle_key_for_application (kf_validator *kf,
                            const char   *locale_key,
                            const char   *value);
static gboolean
handle_key_for_link (kf_validator *kf,
                     const char   *locale_key,
                     const char   *value);
static gboolean
handle_key_for_fsdevice (kf_validator *kf,
                         const char   *locale_key,
                         const char   *value);
static gboolean
handle_key_for_mimetype (kf_validator *kf,
                         const char   *locale_key,
                         const char   *value);

struct {
  DesktopType  type;
  char        *name;
  gboolean     kde_reserved;
  gboolean     deprecated;
} registered_types[] = {
  { APPLICATION_TYPE,  "Application", FALSE, FALSE },
  { LINK_TYPE,         "Link",        FALSE, FALSE },
  { DIRECTORY_TYPE,    "Directory",   FALSE, FALSE },
  { SERVICE_TYPE,      "Service",     TRUE,  FALSE },
  { SERVICE_TYPE_TYPE, "ServiceType", TRUE,  FALSE },
  { FSDEVICE_TYPE,     "FSDevice",    TRUE,  FALSE },
  { MIMETYPE_TYPE,     "MimeType",    FALSE, TRUE  }
};

struct {
  DesktopKeyType type;
  gboolean       (* validate) (kf_validator *kf,
                               const char   *key,
                               const char   *locale,
                               const char   *value);
} validate_for_type[] = {
  { DESKTOP_STRING_TYPE,       validate_string_key       },
  { DESKTOP_LOCALESTRING_TYPE, validate_localestring_key },
  { DESKTOP_BOOLEAN_TYPE,      validate_boolean_key      },
  { DESKTOP_NUMERIC_TYPE,      validate_numeric_key      },
  { DESKTOP_STRING_LIST_TYPE,  validate_string_list_key  },
  { DESKTOP_REGEXP_LIST_TYPE,  validate_regexp_list_key  }
};

struct {
  DesktopKeyType  type;
  char           *name;
  gboolean        required;
  gboolean        deprecated;
  gboolean        kde_reserved;
  gboolean        (* handle_and_validate) (kf_validator *kf,
                                           const char   *locale_key,
                                           const char   *value);
} registered_desktop_keys[] = {
  { DESKTOP_STRING_TYPE,       "Type",              TRUE,  FALSE, FALSE, handle_type_key },
  /* it is numeric according to the spec, but it's not true in previous 
   * versions of the spec. handle_version_key() will manage this */
  { DESKTOP_STRING_TYPE,       "Version",           FALSE, FALSE, FALSE, handle_version_key },
  { DESKTOP_LOCALESTRING_TYPE, "Name",              TRUE,  FALSE, FALSE, NULL },
  { DESKTOP_LOCALESTRING_TYPE, "GenericName",       FALSE, FALSE, FALSE, NULL },
  { DESKTOP_BOOLEAN_TYPE,      "NoDisplay",         FALSE, FALSE, FALSE, NULL },
  { DESKTOP_LOCALESTRING_TYPE, "Comment",           FALSE, FALSE, FALSE, handle_comment_key },
  { DESKTOP_LOCALESTRING_TYPE, "Icon",              FALSE, FALSE, FALSE, NULL },
  { DESKTOP_BOOLEAN_TYPE,      "Hidden",            FALSE, FALSE, FALSE, NULL },
  { DESKTOP_STRING_LIST_TYPE,  "OnlyShowIn",        FALSE, FALSE, FALSE, handle_show_in_key },
  { DESKTOP_STRING_LIST_TYPE,  "NotShowIn",         FALSE, FALSE, FALSE, handle_show_in_key },
  { DESKTOP_STRING_TYPE,       "TryExec",           FALSE, FALSE, FALSE, handle_key_for_application },
  { DESKTOP_STRING_TYPE,       "Exec",              FALSE, FALSE, FALSE, handle_exec_key },
  { DESKTOP_STRING_TYPE,       "Path",              FALSE, FALSE, FALSE, handle_path_key },
  { DESKTOP_BOOLEAN_TYPE,      "Terminal",          FALSE, FALSE, FALSE, handle_key_for_application },
  { DESKTOP_STRING_LIST_TYPE,  "MimeType",          FALSE, FALSE, FALSE, handle_mime_key },
  { DESKTOP_STRING_LIST_TYPE,  "Categories",        FALSE, FALSE, FALSE, handle_categories_key },
  { DESKTOP_BOOLEAN_TYPE,      "StartupNotify",     FALSE, FALSE, FALSE, handle_key_for_application },
  { DESKTOP_STRING_TYPE,       "StartupWMClass",    FALSE, FALSE, FALSE, handle_key_for_application },
  { DESKTOP_STRING_TYPE,       "URL",               FALSE, FALSE, FALSE, handle_key_for_link },

  //FIXME: it's not deprecated, but got removed from the spec temporarly
  { DESKTOP_STRING_LIST_TYPE,  "Actions",           FALSE, FALSE, FALSE, handle_actions_key },

  /* Keys reserved for KDE */

  /* since 0.9.4 */
  { DESKTOP_STRING_TYPE,       "ServiceTypes",      FALSE, FALSE, TRUE,  NULL },
  { DESKTOP_STRING_TYPE,       "DocPath",           FALSE, FALSE, TRUE,  NULL },
  { DESKTOP_LOCALESTRING_TYPE, "Keywords",          FALSE, FALSE, TRUE,  NULL },
  { DESKTOP_STRING_TYPE,       "InitialPreference", FALSE, FALSE, TRUE,  NULL },
  /* since 0.9.6 */
  { DESKTOP_STRING_TYPE,       "Dev",               FALSE, FALSE, TRUE,  handle_dev_key },
  { DESKTOP_STRING_TYPE,       "FSType",            FALSE, FALSE, TRUE,  handle_key_for_fsdevice },
  { DESKTOP_STRING_TYPE,       "MountPoint",        FALSE, FALSE, TRUE,  handle_mountpoint_key },
  { DESKTOP_BOOLEAN_TYPE,      "ReadOnly",          FALSE, FALSE, TRUE,  handle_key_for_fsdevice },
  { DESKTOP_STRING_TYPE,       "UnmountIcon",       FALSE, FALSE, TRUE,  handle_key_for_fsdevice },

  /* Deprecated keys */

  /* since 0.9.3 */
  { DESKTOP_STRING_TYPE,       "Protocols",         FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_STRING_TYPE,       "Extensions",        FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_STRING_TYPE,       "BinaryPattern",     FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_STRING_TYPE,       "MapNotify",         FALSE, TRUE,  FALSE, NULL },
  /* since 0.9.4 */
  { DESKTOP_REGEXP_LIST_TYPE,  "Patterns",          FALSE, TRUE,  FALSE, handle_key_for_mimetype },
  { DESKTOP_STRING_TYPE,       "DefaultApp",        FALSE, TRUE,  FALSE, handle_key_for_mimetype },
  { DESKTOP_STRING_TYPE,       "MiniIcon",          FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_STRING_TYPE,       "TerminalOptions",   FALSE, TRUE,  FALSE, NULL },
  /* since 0.9.5 */
  { DESKTOP_STRING_TYPE,       "Encoding",          FALSE, TRUE,  FALSE, handle_encoding_key },
  { DESKTOP_LOCALESTRING_TYPE, "SwallowTitle",      FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_STRING_TYPE,       "SwallowExec",       FALSE, TRUE,  FALSE, NULL },
  /* since 0.9.6 */
  { DESKTOP_STRING_LIST_TYPE,  "SortOrder",         FALSE, TRUE,  FALSE, NULL },
  { DESKTOP_REGEXP_LIST_TYPE,  "FilePattern",       FALSE, TRUE,  FALSE, NULL }
};

static const char *show_in_registered[] = {
  "KDE", "GNOME", "ROX", "XFCE", "Old"
};

static const char *main_categories_registered[] = {
  "AudioVideo", "Audio", "Video", "Development", "Education", "Game",
  "Graphics", "Network", "Office", "Settings", "System", "Utility"
};

static const char *additional_categories_registered[] = {
  "Building", "Debugger", "IDE", "GUIDesigner", "Profiling", "RevisionControl",
  "Translation", "Calendar", "ContactManagement", "Database", "Dictionary",
  "Chart", "Email", "Finance", "FlowChart", "PDA", "ProjectManagement",
  "Presentation", "Spreadsheet", "WordProcessor", "2DGraphics",
  "VectorGraphics", "RasterGraphics", "3DGraphics", "Scanning", "OCR",
  "Photography", "Publishing", "Viewer", "TextTools", "DesktopSettings",
  "HardwareSettings", "Printing", "PackageManager", "Dialup",
  "InstantMessaging", "Chat", "IRCClient", "FileTransfer", "HamRadio", "News",
  "P2P", "RemoteAccess", "Telephony", "TelephonyTools", "VideoConference",
  "WebBrowser", "WebDevelopment", "Midi", "Mixer", "Sequencer", "Tuner", "TV",
  "AudioVideoEditing", "Player", "Recorder", "DiscBurning", "ActionGame",
  "AdventureGame", "ArcadeGame", "BoardGame", "BlocksGame", "CardGame",
  "KidsGame", "LogicGame", "RolePlaying", "Simulation", "SportsGame",
  "StrategyGame", "Art", "Construction", "Music", "Languages", "Science",
  "ArtificialIntelligence", "Astronomy", "Biology", "Chemistry",
  "ComputerScience", "DataVisualization", "Economy", "Electricity",
  "Geography", "Geology", "Geoscience", "History", "ImageProcessing",
  "Literature", "Math", "NumericalAnalysis", "MedicalSoftware", "Physics",
  "Robotics", "Sports", "ParallelComputing", "Amusement", "Archiving",
  "Compression", "Electronics", "Emulator", "Engineering", "FileTools",
  "FileManager", "TerminalEmulator", "Filesystem", "Monitor", "Security",
  "Accessibility", "Calculator", "Clock", "TextEditor", "Documentation",
  "Core", "KDE", "GNOME", "GTK", "Qt", "Motif", "Java", "ConsoleOnly"
};

static const char *reserved_categories_registered[] = {
  "Screensaver", "TrayIcon", "Applet", "Shell"
};

static const char *deprecated_categories_registered[] = {
  "Application", "Applications"
};

static void
print_fatal (kf_validator *kf, const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (kf != NULL && format != NULL);

  kf->fatal_error = TRUE;

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  g_print ("%s: error: %s", kf->filename, str);
  
  g_free (str);
}

static void
print_warning (kf_validator *kf, const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (kf != NULL && format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  g_print ("%s: warning: %s", kf->filename, str);

  g_free (str);
}

/* + Values of type string may contain all ASCII characters except for control
 *   characters.
 *   Checked.
 */
static gboolean
validate_string_key (kf_validator *kf,
                     const char   *key,
                     const char   *locale,
                     const char   *value)
{
  int      i;
  gboolean error;

  error = FALSE;

  for (i = 0; value[i] != '\0'; i++) {
    if (!g_ascii_isprint (value[i])) {
      error = TRUE;
      break;
    }
  }

  if (error) {
    print_fatal (kf, "value \"%s\" for string key \"%s\" in group \"%s\" "
                     "contains invalid characters, string values may contain "
                     "all ASCII characters except for control characters\n",
                     value, key, kf->main_group);

    return FALSE;
  }

  return TRUE;
}

/* + Values of type localestring are user displayable, and are encoded in
 *   UTF-8.
 *   Checked.
 * + If a postfixed key occurs, the same key must be also present without the
 *   postfix.
 *   Checked.
 */
static gboolean
validate_localestring_key (kf_validator *kf,
                           const char   *key,
                           const char   *locale,
                           const char   *value)
{
  char *locale_key;

  if (locale)
    locale_key = g_strdup_printf ("%s[%s]", key, locale);
  else
    locale_key = g_strdup_printf ("%s", key);

  if (!g_utf8_validate (value, -1, NULL)) {
    print_fatal (kf, "value \"%s\" for locale string key \"%s\" in group "
                     "\"%s\" contains invalid UTF-8 characters, locale string "
                     "values should be encoded in UTF-8\n",
                     value, locale_key, kf->main_group);
    g_free (locale_key);

    return FALSE;
  }

  if (!g_key_file_has_key (kf->keyfile, kf->main_group, key, NULL)) {
    print_fatal (kf, "key \"%s\" in group \"%s\" is a localized key, but "
                     "there is no non-localized key \"%s\"\n",
                     locale_key, kf->main_group, key);
    g_free (locale_key);

    return FALSE;
  }

  g_free (locale_key);

  return TRUE;
}

/* + Values of type boolean must either be the string true or false.
 *   Checked.
 * + Historically some booleans have been represented by the numeric entries 0
 *   or 1. With this version of the standard they are now to be represented as
 *   a boolean string. However, if an implementation is reading a pre-1.0
 *   desktop entry, it should interpret 0 and 1 as false and true,
 *   respectively.
 *   Checked.
 */
static gboolean
validate_boolean_key (kf_validator *kf,
                      const char   *key,
                      const char   *locale,
                      const char   *value)
{
  if (strcmp (value, "true") && strcmp (value, "false") &&
      strcmp (value, "0")    && strcmp (value, "1")) {
    print_fatal (kf, "value \"%s\" for boolean key \"%s\" in group \"%s\" "
                     "contains invalid characters, boolean values must be "
                     "\"false\" or \"true\"\n",
                     value, key, kf->main_group);
    return FALSE;
  }

  if (!kf->no_deprecated_warnings &&
      (!strcmp (value, "0") || !strcmp (value, "1")))
    print_warning (kf, "boolean key \"%s\" in group \"%s\" has value \"%s\", "
                       "which is deprecated: boolean values should be "
                       "\"false\" or \"true\"\n",
                       key, kf->main_group, value);

  return TRUE;
}

/* + Values of type numeric must be a valid floating point number as recognized
 *   by the %f specifier for scanf.
 *   Checked.
 */
static gboolean
validate_numeric_key (kf_validator *kf,
                      const char   *key,
                      const char   *locale,
                      const char   *value)
{
  float d;
  int res;
  
  res = sscanf (value, "%f", &d);
  if (res == 0) {
    print_fatal (kf, "value \"%s\" for numeric key \"%s\" in group \"%s\" "
                     "contains invalid characters, numeric values must be "
                     "valid floating point numbers\n",
                     value, key, kf->main_group);
    return FALSE;
  }

  return TRUE;
}

/* + Values of type string may contain all ASCII characters except for control
 *   characters.
 *   Checked.
 * + The multiple values should be separated by a semicolon. Those keys which
 *   have several values should have a semicolon as the trailing character.
 *   Checked.
 * + FIXME: how should an empty list be handled?
 */
static gboolean
validate_string_regexp_list_key (kf_validator *kf,
                                 const char   *key,
                                 const char   *locale,
                                 const char   *value,
                                 const char   *type)
{
  int      i;
  gboolean error;

  error = FALSE;

  for (i = 0; value[i] != '\0'; i++) {
    if (!g_ascii_isprint (value[i])) {
      error = TRUE;
      break;
    }
  }

  if (error) {
    print_fatal (kf, "value \"%s\" for %s list key \"%s\" in group \"%s\" "
                     "contains invalid character '%c', %s list values may "
                     "contain all ASCII characters except for control "
                     "characters\n",
                     value, type, key, kf->main_group, value[i], type);

    return FALSE;
  }

  if (i > 0 && value[i - 1] != ';') {
    print_fatal (kf, "value \"%s\" for %s list key \"%s\" in group \"%s\" "
                     "does not have a semicolon (';') as trailing "
                     "character\n",
                     value, type, key, kf->main_group);

    return FALSE;
  }

  if (i > 1 && value[i - 1] == ';' && value[i - 2] == '\\' &&
      (i < 3 || value[i - 3] != '\\')) {
    print_fatal (kf, "value \"%s\" for %s list key \"%s\" in group \"%s\" "
                     "has an escaped semicolon (';') as trailing character\n",
                     value, type, key, kf->main_group);

    return FALSE;
  }

  return TRUE;
}

static gboolean
validate_string_list_key (kf_validator *kf,
                          const char   *key,
                          const char   *locale,
                          const char   *value)
{
  return validate_string_regexp_list_key (kf, key, locale, value, "string");
}

static gboolean
validate_regexp_list_key (kf_validator *kf,
                          const char   *key,
                          const char   *locale,
                          const char   *value)
{
  return validate_string_regexp_list_key (kf, key, locale, value, "regexp");
}

/* + This specification defines 3 types of desktop entries: Application
 *   (type 1), Link (type 2) and Directory (type 3). To allow the addition of
 *   new types in the future, implementations should ignore desktop entries
 *   with an unknown type.
 *   Checked.
 * + KDE specific types: ServiceType, Service and FSDevice
 *   Checked.
 */
static gboolean
handle_type_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (registered_types); i++) {
    if (!strcmp (value, registered_types[i].name))
      break;
  }

  if (i == G_N_ELEMENTS (registered_types)) {
    /* force the type, since the key might be present multiple times... */
    kf->type = INVALID_TYPE;

    print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                     "is not a registered type value (\"Application\", "
                     "\"Link\" and \"Directory\")\n",
                     value, locale_key, kf->main_group);
    return FALSE;
  }

  if (registered_types[i].kde_reserved && kf->kde_reserved_warnings)
    print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "is a reserved value for KDE\n",
                       value, locale_key, kf->main_group);

  if (registered_types[i].deprecated && !kf->no_deprecated_warnings)
    print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "is deprecated\n",
                       value, locale_key, kf->main_group);

  kf->type = registered_types[i].type;
  kf->type_string = registered_types[i].name;

  return TRUE;
}

/* + Entries that confirm with this version of the specification should use
 *   1.0.
 *   Checked.
 * + Previous versions of the spec: 0.9.x where 3 <= x <= 8
 *   Checked.
 */
static gboolean
handle_version_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value)
{
  if (!strcmp (value, "1.0"))
    return TRUE;

  if (!strncmp (value, "0.9.", strlen ("0.9."))) {
    char c;

    c = value[strlen ("0.9.")];
    if ('3' <= c && c <= '8' && value[strlen ("0.9.") + 1] == '\0')
      return TRUE;
  }

  print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                   "is not a known version\n",
                   value, locale_key, kf->main_group);
  return FALSE;
}

/* + Tooltip for the entry, for example "View sites on the Internet", should
 *   not be redundant with Name or GenericName.
 *   FIXME
 */
static gboolean
handle_comment_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value)
{
  return TRUE;
}

/* + Only one of these keys, either OnlyShowIn or NotShowIn, may appear in a
 *   group.
 *   Checked.
 * + (for possible values see the Desktop Menu Specification)
 *   Checked.
 *   FIXME: this is not perfect because it could fail if a new value with
 *   a semicolon is registered.
 */
static gboolean
handle_show_in_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value)
{
  gboolean       retval;
  char         **show;
  GHashTable    *hashtable;
  int            i;
  unsigned int   j;

  retval = TRUE;

  if (kf->show_in) {
    print_fatal (kf, "only one of \"OnlyShowIn\" and \"NotShowInkey\" keys "
                     "may appear in group \"%s\"\n",
                     kf->main_group);
    retval = FALSE;
  }
  kf->show_in = TRUE;

  hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  show = g_strsplit (value, ";", 0);

  for (i = 0; show[i]; i++) {
    /* since the value ends with a semicolon, we'll have an empty string
     * at the end */
    if (*show[i] == '\0' && show[i + 1] == NULL)
      break;

    if (g_hash_table_lookup (hashtable, show[i])) {
      print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                         "contains \"%s\" more than once\n",
                         value, locale_key, kf->main_group, show[i]);
      continue;
    }

    g_hash_table_insert (hashtable, show[i], show[i]);

    for (j = 0; j < G_N_ELEMENTS (show_in_registered); j++) {
      if (!strcmp (show[i], show_in_registered[j]))
        break;
    }

    if (j == G_N_ELEMENTS (show_in_registered)) {
      print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "contains an unregistered value \"%s\"\n",
                       value, locale_key, kf->main_group, show[i]);
      retval = FALSE;
    }
  }

  g_strfreev (show);
  g_hash_table_destroy (hashtable);

  return retval;
}

/* + A command line consists of an executable program optionally followed by
 *   one or more arguments. The executable program can either be specified with
 *   its full path or with the name of the executable only. If no full path is
 *   provided the executable is looked up in the $PATH used by the desktop
 *   environment. The name or path of the executable program may not contain
 *   the equal sign ("=").
 *   FIXME
 * + Arguments are separated by a space.
 *   FIXME
 * + Arguments may be quoted in whole.
 *   FIXME
 * + If an argument contains a reserved character the argument must be quoted.
 *   Checked.
 * + The rules for quoting of arguments is also applicable to the executable
 *   name or path of the executable program as provided.
 *   FIXME
 * + Quoting must be done by enclosing the argument between double quotes and
 *   escaping the double quote character, backtick character ("`"), dollar sign
 *   ("$") and backslash character ("\") by preceding it with an additional
 *   backslash character. Implementations must undo quoting before expanding
 *   field codes and before passing the argument to the executable program.
 *   Reserved characters are space (" "), tab, newline, double quote, single
 *   quote ("'"), backslash character ("\"), greater-than sign (">"), less-than
 *   sign ("<"), tilde ("~"), vertical bar ("|"), ampersand ("&"), semicolon
 *   (";"), dollar sign ("$"), asterisk ("*"), question mark ("?"), hash mark
 *   ("#"), parenthesis ("(") and (")") and backtick character ("`").
 *   Checked.
 * + Note that the general escape rule for values of type string states that
 *   the backslash character can be escaped as ("\\") as well and that this
 *   escape rule is applied before the quoting rule. As such, to unambiguously
 *   represent a literal backslash character in a quoted argument in a desktop
 *   entry file requires the use of four successive backslash characters
 *   ("\\\\"). Likewise, a literal dollar sign in a quoted argument in a
 *   desktop entry file is unambiguously represented with ("\\$").
 *   Checked.
 * + Field codes consist of the percentage character ("%") followed by an alpha
 *   character. Literal percentage characters must be escaped as %%.
 *   Checked.
 * + Command lines that contain a field code that is not listed in this
 *   specification are invalid and must not be processed, in particular
 *   implementations may not introduce support for field codes not listed in
 *   this specification. Extensions, if any, should be introduced by means of a
 *   new key.
 *   Checked.
 * + A command line may contain at most one %f, %u, %F or %U field code.
 *   Checked.
 * + The %F and %U field codes may only be used as an argument on their own.
 *   FIXME
 */
static gboolean
handle_exec_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value)
{
  gboolean    retval;
  gboolean    file_uri;
  gboolean    in_quote;
  gboolean    escaped;
  gboolean    flag;
  const char *c;

  handle_key_for_application (kf, locale_key, value);

  retval = TRUE;

  file_uri = FALSE;
  in_quote = FALSE;
  escaped  = FALSE;
  flag     = FALSE;

#define PRINT_INVALID_IF_FLAG                                       \
  if (flag) {                                                       \
    print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" " \
                     "contains an invalid field code \"%%%c\"\n",   \
                     value, locale_key, kf->main_group, *c);   \
    retval = FALSE;                                                 \
    flag = FALSE;                                                   \
    break;                                                          \
  }

  c = value;
  while (*c) {
    switch (*c) {
      /* quotes and escaped characters in quotes */
      case '"':
        PRINT_INVALID_IF_FLAG;
        if (in_quote) {
          if (!escaped)
            in_quote = FALSE;
        } else {
          if (!escaped)
            in_quote = TRUE;
          else {
            print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                             "contains an escaped double quote (\\\\\") "
                             "outside of a quote, but the double quote is "
                             "a reserved character\n",
                             value, locale_key, kf->main_group);
            retval = FALSE;

            escaped = FALSE;
          }
        }
        break;
      case '`':
      case '$':
        PRINT_INVALID_IF_FLAG;
        if (in_quote) {
          if (!escaped) {
            print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                             "contains a non-escaped character '%c' in a "
                             "quote, but it should be escaped with two "
                             "backslashes (\"\\\\%c\")\n",
                             value, locale_key, kf->main_group, *c, *c);
            retval = FALSE;
          } else
            escaped = FALSE;
        } else {
          print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                           "contains a reserved character '%c' outside of a "
                           "quote\n",
                           value, locale_key, kf->main_group, *c);
          retval = FALSE;
        }
        break;
      case '\\':
        PRINT_INVALID_IF_FLAG;
        c++;
        if (*c == '\\' && in_quote)
          escaped = !escaped;
        break;

      /* reserved characters */
      case ' ':
        //FIXME
        break;
      case '\t':
      case '\n':
      case '\'':
      case '>':
      case '<':
      case '~':
      case '|':
      case '&':
      case ';':
      case '*':
      case '?':
      case '#':
      case '(':
      case ')':
        PRINT_INVALID_IF_FLAG;
        if (!in_quote) {
          print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                           "contains a reserved character '%c' outside of a "
                           "quote\n",
                           value, locale_key, kf->main_group, *c);
          retval = FALSE;
        }
        break;

      /* flags */
      case '%':
        flag = !flag;
        break;
      case 'f':
      case 'u':
        if (flag) {
          if (file_uri) {
            print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                             "may contain at most one \"%f\", \"%u\", "
                             "\"%F\" or \"%U\" field code\n",
                             value, locale_key, kf->main_group);
            retval = FALSE;
          }

          file_uri = TRUE;
          flag = FALSE;
        }
        break;
      case 'F':
      case 'U':
        if (flag) {
          if (file_uri) {
            print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                             "may contain at most one \"%f\", \"%u\", "
                             "\"%F\" or \"%U\" field code\n",
                             value, locale_key, kf->main_group);
            retval = FALSE;
          }

          file_uri = TRUE;
          flag = FALSE;
        }
        break;
      case 'i':
      case 'c':
      case 'k':
        if (flag)
          flag = FALSE;
        break;
      case 'd':
      case 'D':
      case 'n':
      case 'N':
      case 'v':
      case 'm':
        if (flag) {
          if (!kf->no_deprecated_warnings)
            print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                               "contains a deprecated field code \"%%%c\"\n",
                                value, locale_key, kf->main_group, *c);
          flag = FALSE;
        }
        break;

      default:
        PRINT_INVALID_IF_FLAG;
        break;
    }

    c++;
  }

  if (in_quote) {
    print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" contains a "
                     "quote which is not closed\n",
                     value, locale_key, kf->main_group);
    retval = FALSE;
  }

  if (flag) {
    print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" contains a "
                     "non-complete field code\n",
                     value, locale_key, kf->main_group);
    retval = FALSE;
  }

  return retval;
}

/* + If entry is of type Application, the working directory to run the program
 *   in. (probably implies an absolute path)
 *   Checked.
 */
static gboolean
handle_path_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value)
{
  handle_key_for_application (kf, locale_key, value);

  if (!g_path_is_absolute (value))
    print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "does not look like an absolute path\n",
                       value, locale_key, kf->main_group);

  return TRUE;
}

/* + The MIME type(s) supported by this application. Check they are valid
 *   MIME types.
 *   Checked.
 *   FIXME: need to verify what is the exact definition of a MIME type.
 *   Look at is_valid_mime_type()
 */
static gboolean
handle_mime_key (kf_validator *kf,
                 const char   *locale_key,
                 const char   *value)
{
  gboolean       retval;
  char         **types;
  char          *slash;
  GHashTable    *hashtable;
  int            i;

  handle_key_for_application (kf, locale_key, value);

  retval = TRUE;

  hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  types = g_strsplit (value, ";", 0);

  for (i = 0; types[i]; i++) {
    /* since the value ends with a semicolon, we'll have an empty string
     * at the end */
    if (*types[i] == '\0' && types[i + 1] == NULL)
      break;

    if (g_hash_table_lookup (hashtable, types[i])) {
      print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                         "contains \"%s\" more than once\n",
                         value, locale_key, kf->main_group, types[i]);
      continue;
    }

    g_hash_table_insert (hashtable, types[i], types[i]);

    slash = strchr (types[i], '/');
    if (!slash || strchr (slash + 1, '/')) {
      print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "contains value \"%s\" which does not look like "
                       "a MIME type\n",
                       value, locale_key, kf->main_group, types[i]);
      retval = FALSE;
    }
  }

  g_strfreev (types);
  g_hash_table_destroy (hashtable);

  return retval;
}

/* + FIXME: is there restrictions on how a category should be named?
 * + Categories in which the entry should be shown in a menu (for possible
 *   values see the Desktop Menu Specification).
 *   Checked.
 * + The table below describes Reserved Categories. Reserved Categories have a
 *   specific desktop specific meaning that has not been standardized (yet).
 *   Desktop entry files that use a reserved category MUST also include an
 *   appropriate OnlyShowIn= entry to restrict themselves to those environments
 *   that properly support the reserved category as used.
 *   Checked.
 * + Accept "Application" as a deprecated category.
 *   Checked.
 *   FIXME: it's not really deprecated, so the error message is wrong
 * + All categories extending the format should start with "X-".
 *   Checked.
 */
static gboolean
handle_categories_key (kf_validator *kf,
                       const char   *locale_key,
                       const char   *value)
{
  gboolean       retval;
  char         **categories;
  GHashTable    *hashtable;
  int            i;
  unsigned int   j;

  handle_key_for_application (kf, locale_key, value);

  retval = TRUE;

  hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  categories = g_strsplit (value, ";", 0);

  for (i = 0; categories[i]; i++) {
    /* since the value ends with a semicolon, we'll have an empty string
     * at the end */
    if (*categories[i] == '\0' && categories[i + 1] == NULL)
      break;

    if (g_hash_table_lookup (hashtable, categories[i])) {
      print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                         "contains \"%s\" more than once\n",
                         value, locale_key, kf->main_group, categories[i]);
      continue;
    }

    g_hash_table_insert (hashtable, categories[i], categories[i]);

    if (!strncmp (categories[i], "X-", 2))
      continue;

#define IF_CHECK_REGISTERED_CATEGORIES(table) \
    for (j = 0; j < G_N_ELEMENTS (table); j++) { \
      if (!strcmp (categories[i], table[j]))     \
        break;                                   \
    }                                            \
    if (j != G_N_ELEMENTS (table))

    IF_CHECK_REGISTERED_CATEGORIES (main_categories_registered)
      continue;
    IF_CHECK_REGISTERED_CATEGORIES (additional_categories_registered)
      continue;
    IF_CHECK_REGISTERED_CATEGORIES (reserved_categories_registered) {
      if (!g_key_file_has_key (kf->keyfile, kf->main_group,
                               "OnlyShowIn", NULL)) {
        print_fatal (kf, "value \"%s\" in key \"%s\" in group \"%s\" "
                         "is a reserved category, so a \"OnlyShowIn\" key "
                         "must be included\n",
                         categories[i], locale_key, kf->main_group);
        retval = FALSE;
      }
      continue;
    }
    IF_CHECK_REGISTERED_CATEGORIES (deprecated_categories_registered) {
      if (!kf->no_deprecated_warnings)
        print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                           "contains a deprecated value \"%s\"\n",
                            value, locale_key, kf->main_group,
                            categories[i]);
      continue;
    }

    print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                     "contains an unregistered value \"%s\"\n",
                     value, locale_key, kf->main_group, categories[i]);
    retval = FALSE;
  }

  g_strfreev (categories);
  g_hash_table_destroy (hashtable);

  return retval;
}

/* FIXME: we don't know the format for this, so we'll just assume that it's
 * always valid...
 * This could be wrong because we could use the characters that are
 * valid for a group name. And also, since it's strings, it should be only
 * characters accepted for string values.
 */
static gboolean
handle_actions_key (kf_validator *kf,
                    const char   *locale_key,
                    const char   *value)
{
  char **actions;
  char  *action;
  int    i;

  actions = g_strsplit (value, ";", 0);

  for (i = 0; actions[i]; i++) {
    /* since the value ends with a semicolon, we'll have an empty string
     * at the end */
    if (*actions[i] == '\0') {
      if (actions[i + 1] != NULL)
        print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                           "contains an empty action\n",
                           value, locale_key, kf->main_group);
      continue;
    }

    if (g_hash_table_lookup (kf->action_values, actions[i])) {
      print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                         "contains action \"%s\" more than once\n",
                         value, locale_key, kf->main_group, actions[i]);
      continue;
    }

    action = g_strdup (actions[i]);
    g_hash_table_insert (kf->action_values, action, action);
  }

  g_strfreev (actions);

  return TRUE;
}

/* + The device to mount. (probably implies an absolute path)
 *   Checked.
 */
static gboolean
handle_dev_key (kf_validator *kf,
                const char   *locale_key,
                const char   *value)
{
  handle_key_for_fsdevice (kf, locale_key, value);

  if (!g_path_is_absolute (value))
    print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "does not look like an absolute path\n",
                       value, locale_key, kf->main_group);

  return TRUE;
}

/* + The mount point of the device in question. (probably implies an absolute
 *   path)
 *   Checked.
 */
static gboolean
handle_mountpoint_key (kf_validator *kf,
                       const char   *locale_key,
                       const char   *value)
{
  handle_key_for_fsdevice (kf, locale_key, value);

  if (!g_path_is_absolute (value))
    print_warning (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                       "does not look like an absolute path\n",
                       value, locale_key, kf->main_group);

  return TRUE;
}

/* + Possible values are UTF-8 and Legacy-Mixed.
 *   Checked.
 */
static gboolean
handle_encoding_key (kf_validator *kf,
                     const char   *locale_key,
                     const char   *value)
{
  if (!strcmp (value, "UTF-8") || !strcmp (value, "Legacy-Mixed"))
    return TRUE;

  print_fatal (kf, "value \"%s\" for key \"%s\" in group \"%s\" "
                   "is not a registered encoding value (\"UTF-8\", and "
                   "\"Legacy-Mixed\")\n",
                   value, locale_key, kf->main_group);

  return FALSE;
}

static gboolean
handle_key_for_application (kf_validator *kf,
                            const char   *locale_key,
                            const char   *value)
{
  kf->application_keys = g_list_append (kf->application_keys,
                                        g_strdup (locale_key));
  return TRUE;
}

static gboolean
handle_key_for_link (kf_validator *kf,
                     const char   *locale_key,
                     const char   *value)
{
  kf->link_keys = g_list_append (kf->link_keys,
                                 g_strdup (locale_key));
  return TRUE;
}

static gboolean
handle_key_for_fsdevice (kf_validator *kf,
                         const char   *locale_key,
                         const char   *value)
{
  kf->fsdevice_keys = g_list_append (kf->fsdevice_keys,
                                     g_strdup (locale_key));
  return TRUE;
}

static gboolean
handle_key_for_mimetype (kf_validator *kf,
                         const char   *locale_key,
                         const char   *value)
{
  kf->mimetype_keys = g_list_append (kf->mimetype_keys,
                                     g_strdup (locale_key));
  return TRUE;
}

/* + Key names must contain only the characters A-Za-z0-9-.
 *   Checked.
 * + LOCALE must be of the form lang_COUNTRY.ENCODING@MODIFIER, where _COUNTRY,
 *   .ENCODING, and @MODIFIER  may be omitted.
 *   Checked.
 */
static gboolean
key_extract_locale (const char  *key,
                    char       **real_key,
                    char       **locale)
{
  const char *start_locale;
  char        c;
  int         len;
  int         i;

  if (real_key)
    *real_key = NULL;
  if (locale)
    *locale = NULL;

  start_locale = g_strrstr (key, "[");

  if (start_locale)
    len = start_locale - key;
  else
    len = strlen (key);

  for (i = 0; i < len; i++) {
    c = key[i];
    if (!g_ascii_isalnum (c) && c != '-')
      return FALSE;
  }

  if (!start_locale) {
    if (real_key)
      *real_key = g_strdup (key);
    if (locale)
      *locale = NULL;

    return TRUE;
  }

  len = strlen (start_locale);
  if (len <= 2 || start_locale[len - 1] != ']')
    return FALSE;

  /* ignore first [ and last ] */
  for (i = 1; i < len - 2; i++) {
    c = start_locale[i];
    if (!g_ascii_isalnum (c) && c != '-' && c != '_' && c != '.' && c != '@')
      return FALSE;
  }

  if (real_key)
    *real_key = g_strndup (key, strlen (key) - len);
  if (locale)
    *locale = g_strndup (start_locale + 1, len - 2);

  return TRUE;
}

/* + All keys extending the format should start with "X-".
 *   Checked.
 */
static gboolean
validate_desktop_key (kf_validator *kf,
                      const char   *locale_key,
                      const char   *key,
                      const char   *locale)
{
  unsigned int i;
  unsigned int j;
  char         *value;

  if (!strncmp (key, "X-", 2))
    return TRUE;

  for (i = 0; i < G_N_ELEMENTS (registered_desktop_keys); i++) {
    if (strcmp (key, registered_desktop_keys[i].name))
      continue;

    if (registered_desktop_keys[i].type != DESKTOP_LOCALESTRING_TYPE &&
        locale != NULL) {
      print_fatal (kf, "file contains key \"%s\" in group \"%s\", "
                       "but \"%s\" is not defined as a locale string\n",
                       locale_key, kf->main_group, key);
      return FALSE;
    }

    for (j = 0; j < G_N_ELEMENTS (validate_for_type); j++) {
      if (validate_for_type[j].type == registered_desktop_keys[i].type)
        break;
    }

    g_assert (j != G_N_ELEMENTS (validate_for_type));

    if (!kf->no_deprecated_warnings && registered_desktop_keys[i].deprecated)
      print_warning (kf, "key \"%s\" in group \"%s\" is deprecated\n",
                         locale_key, kf->main_group);

    if (registered_desktop_keys[i].kde_reserved && kf->kde_reserved_warnings)
      print_warning (kf, "key \"%s\" in group \"%s\" is a reserved key for "
                         "KDE\n",
                         locale_key, kf->main_group);

    value = g_key_file_get_value (kf->keyfile, kf->main_group,
                                  locale_key, NULL);

    /* this is not supposed to happen since we got the key from the list of
     * existing keys */
    g_assert (value != NULL);

    if (!validate_for_type[j].validate (kf, key, locale, value)) {
      g_free (value);
      return FALSE;
    }

    if (registered_desktop_keys[i].handle_and_validate != NULL) {
      if (!registered_desktop_keys[i].handle_and_validate (kf, locale_key,
                                                           value)) {
        g_free (value);
        return FALSE;
      }
    }

    g_free (value);

    break;
  }

  if (i == G_N_ELEMENTS (registered_desktop_keys)) {
    print_fatal (kf, "file contains key \"%s\" in group \"%s\", but "
                     "keys extending the format should start with "
                     "\"X-\"\n", key, kf->main_group);
    return FALSE;
  }

  return TRUE;
}

/* + Multiple keys in the same group may not have the same name.
 *   Checked.
 */
static gboolean
validate_keys_for_group (kf_validator *kf,
                         const char   *group)
{
  gboolean     desktop_group;
  gboolean     retval;
  int          i;
  char        *key;
  char        *locale;
  char       **keys;
  GHashTable  *hashtable;

  retval = TRUE;

  desktop_group = (!strcmp (group, GROUP_DESKTOP_ENTRY) ||
                   !strcmp (group, GROUP_KDE_DESKTOP_ENTRY));

  keys = g_key_file_get_keys (kf->keyfile, group, NULL, NULL);

  g_assert (keys != NULL);

  hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  for (i = 0; keys[i] != NULL; i++) {
    if (!key_extract_locale (keys[i], &key, &locale)) {
        print_fatal (kf, "file contains key \"%s\" in group \"%s\", but "
                         "key names must contain only the characters "
                         "A-Za-z0-9- (they may have a \"[LOCALE]\" postfix)\n",
                         keys[i], group);
        retval = FALSE;

        key = g_strdup (keys[i]);
    }

    g_assert (key != NULL);

    if (g_hash_table_lookup (hashtable, keys[i])) {
      print_fatal (kf, "file contains multiple keys named \"%s\" in "
                       "group \"%s\"\n", keys[i], group);
      retval = FALSE;
    }

    if (desktop_group) {
      if (!validate_desktop_key (kf, keys[i], key, locale))
        retval = FALSE;
    }

    g_hash_table_insert (hashtable, keys[i], keys[i]);

    g_free (key);
    key = NULL;
    g_free (locale);
    locale = NULL;
  }

  g_hash_table_destroy (hashtable);
  g_strfreev (keys);

  return retval;
}

static gboolean
validate_group_name (kf_validator *kf,
                     const char   *group)
{
  int  i;
  char c;

  for (i = 0; group[i] != '\0'; i++) {
    c = group[i];
    if (!g_ascii_isprint (c) || c == '[' || c == ']') {
      print_fatal (kf, "file contains group \"%s\", but group names "
                       "may contain all ASCII characters except for [ "
                       "and ] and control characters\n", group);
      return FALSE;
    }
  }

  if (!strncmp (group, "X-", 2))
    return TRUE;

  if (!strcmp (group, GROUP_DESKTOP_ENTRY)) {
    if (kf->main_group && !strcmp (kf->main_group, GROUP_KDE_DESKTOP_ENTRY))
      print_warning (kf, "file contains groups \"%s\" and \"%s\", which play "
                         "the same role\n",
                         GROUP_KDE_DESKTOP_ENTRY, GROUP_DESKTOP_ENTRY);

    kf->main_group = GROUP_DESKTOP_ENTRY;

    return TRUE;
  }

  if (!strcmp (group, GROUP_KDE_DESKTOP_ENTRY)) {
    if (kf->kde_reserved_warnings || !kf->no_deprecated_warnings)
      print_warning (kf, "file contains group \"%s\", which is deprecated "
                         "in favor of \"%s\"\n", group, GROUP_DESKTOP_ENTRY);

    if (kf->main_group && !strcmp (kf->main_group, GROUP_DESKTOP_ENTRY))
      print_warning (kf, "file contains groups \"%s\" and \"%s\", which play "
                         "the same role\n",
                         GROUP_DESKTOP_ENTRY, GROUP_KDE_DESKTOP_ENTRY);

    kf->main_group = GROUP_KDE_DESKTOP_ENTRY;

    return TRUE;
  }

  if (!strncmp (group, GROUP_DESKTOP_ACTION, strlen (GROUP_DESKTOP_ACTION))) {
    if (group[strlen (GROUP_DESKTOP_ACTION) - 1] == '\0') {
      print_fatal (kf, "file contains group \"%s\", which is an action "
                       "group with no action name\n", group);
      return FALSE;
    } else {
      char *action;

      action = g_strdup (group + strlen (GROUP_DESKTOP_ACTION));
      g_hash_table_insert (kf->action_groups, action, action);

      return TRUE;
    }
  }

  print_fatal (kf, "file contains group \"%s\", but groups extending "
                   "the format should start with \"X-\"\n", group);
  return FALSE;
}

/* + Only comments are accepted before the first group.
 *   FIXME: verify that GKeyFile handles this.
 * + Using [KDE Desktop Entry] instead of [Desktop Entry] as header is
 *   deprecated.
 *   Checked.
 * + The first group should be "Desktop Entry".
 *   Checked.
 * + Group names may contain all ASCII characters except for [ and ] and
 *   control characters.
 *   Checked.
 * + Multiple groups may not have the same name.
 *   FIXME: GKeyFile can't let us verify this.
 * + All groups extending the format should start with "X-".
 *   Checked.
 * + Accept "Desktop Action foobar" group if the value for the Action key
 *   contains "foobar". (This is not in spec 1.0, but it was there before and
 *   it wasn't deprecated)
 *   Checked.
 */
static gboolean
validate_groups_and_keys (kf_validator *kf)
{
  gboolean   retval;
  int        i;
  char      *group;
  char     **groups;

  retval = TRUE;

  group = g_key_file_get_start_group (kf->keyfile);
  if (!group ||
      (strcmp (group, GROUP_DESKTOP_ENTRY) &&
       strcmp (group, GROUP_KDE_DESKTOP_ENTRY)))
    print_fatal (kf, "first group is not \"" GROUP_DESKTOP_ENTRY "\"\n");
  g_free (group);

  groups = g_key_file_get_groups (kf->keyfile, NULL);
  i = 0;
  for (i = 0; groups[i] != NULL; i++) {
    group = groups[i];

    if (!validate_group_name (kf, group))
      retval = FALSE;

    if (!validate_keys_for_group (kf, group))
      retval = FALSE;
  }

  g_strfreev (groups);
  return retval;
}

static gboolean
validate_required_keys (kf_validator *kf)
{
  gboolean     retval;
  unsigned int i;

  retval = TRUE;

  for (i = 0; i < G_N_ELEMENTS (registered_desktop_keys); i++) {
    if (registered_desktop_keys[i].required) {
      if (!g_key_file_has_key (kf->keyfile, kf->main_group,
                               registered_desktop_keys[i].name, NULL)) {
        print_fatal (kf, "required key \"%s\" in group \"%s\" is not "
                         "present\n",
                         registered_desktop_keys[i].name, kf->main_group);
        retval = FALSE;
      }
    }
  }

  return retval;
}

#define PRINT_ERROR_FOREACH_KEY(lower, real)                                 \
static void                                                                  \
print_error_foreach_##lower##_key (const char   *name,                       \
                                   kf_validator *kf)                         \
{                                                                            \
  print_fatal (kf, "key \"%s\" is present in group \"%s\", but the type is " \
                   "\"%s\" while this key is only valid for type \"%s\"\n",  \
                   name, kf->main_group, kf->type_string, real);             \
}

PRINT_ERROR_FOREACH_KEY (application, "Application")
PRINT_ERROR_FOREACH_KEY (link,        "Link")
PRINT_ERROR_FOREACH_KEY (fsdevice,    "FSDevice")
PRINT_ERROR_FOREACH_KEY (mimetype,    "MimeType")

static gboolean
validate_type_keys (kf_validator *kf)
{
  gboolean retval;

  retval = TRUE;

  switch (kf->type) {
    case INVALID_TYPE:
      break;
    case APPLICATION_TYPE:
      g_list_foreach (kf->link_keys,
                      (GFunc) print_error_foreach_link_key, kf);
      g_list_foreach (kf->fsdevice_keys,
                      (GFunc) print_error_foreach_fsdevice_key, kf);
      g_list_foreach (kf->mimetype_keys,
                      (GFunc) print_error_foreach_mimetype_key, kf);
      retval = (g_list_length (kf->link_keys) +
                g_list_length (kf->fsdevice_keys) +
                g_list_length (kf->mimetype_keys) == 0);
      break;
    case LINK_TYPE:
      g_list_foreach (kf->application_keys,
                      (GFunc) print_error_foreach_application_key, kf);
      g_list_foreach (kf->fsdevice_keys,
                      (GFunc) print_error_foreach_fsdevice_key, kf);
      g_list_foreach (kf->mimetype_keys,
                      (GFunc) print_error_foreach_mimetype_key, kf);
      retval = (g_list_length (kf->application_keys) +
                g_list_length (kf->fsdevice_keys) +
                g_list_length (kf->mimetype_keys) == 0);
      break;
    case DIRECTORY_TYPE:
    case SERVICE_TYPE:
    case SERVICE_TYPE_TYPE:
      g_list_foreach (kf->application_keys,
                      (GFunc) print_error_foreach_application_key, kf);
      g_list_foreach (kf->link_keys,
                      (GFunc) print_error_foreach_link_key, kf);
      g_list_foreach (kf->fsdevice_keys,
                      (GFunc) print_error_foreach_fsdevice_key, kf);
      g_list_foreach (kf->mimetype_keys,
                      (GFunc) print_error_foreach_mimetype_key, kf);
      retval = (g_list_length (kf->application_keys) +
                g_list_length (kf->link_keys) +
                g_list_length (kf->fsdevice_keys) +
                g_list_length (kf->mimetype_keys) == 0);
      break;
    case FSDEVICE_TYPE:
      g_list_foreach (kf->application_keys,
                      (GFunc) print_error_foreach_application_key, kf);
      g_list_foreach (kf->link_keys,
                      (GFunc) print_error_foreach_link_key, kf);
      g_list_foreach (kf->mimetype_keys,
                      (GFunc) print_error_foreach_mimetype_key, kf);
      retval = (g_list_length (kf->application_keys) +
                g_list_length (kf->link_keys) +
                g_list_length (kf->mimetype_keys) == 0);
      break;
    case MIMETYPE_TYPE:
      g_list_foreach (kf->application_keys,
                      (GFunc) print_error_foreach_application_key, kf);
      g_list_foreach (kf->link_keys,
                      (GFunc) print_error_foreach_link_key, kf);
      g_list_foreach (kf->fsdevice_keys,
                      (GFunc) print_error_foreach_fsdevice_key, kf);
      retval = (g_list_length (kf->application_keys) +
                g_list_length (kf->link_keys) +
                g_list_length (kf->fsdevice_keys) == 0);
      break;
    case LAST_TYPE:
      g_assert_not_reached ();
  }

  return retval;
}

static gboolean
lookup_group_foreach_action (char         *key,
                             char         *value,
                             kf_validator *kf)
{
  if (g_hash_table_lookup (kf->action_groups, key)) {
    g_hash_table_remove (kf->action_groups, key);
    return TRUE;
  }

  return FALSE;
}

static void
print_error_foreach_action (char         *key,
                            char         *value,
                            kf_validator *kf)
{
  print_fatal (kf, "action \"%s\" is defined, but there is no matching "
                   "\"%s%s\" group\n", key, GROUP_DESKTOP_ACTION, key);
}

static void
print_error_foreach_group (char         *key,
                           char         *value,
                           kf_validator *kf)
{
  print_fatal (kf, "action group \"%s%s\" exists, but there is no matching "
                   "action \"%s\"\n", GROUP_DESKTOP_ACTION, key, key);
}

static gboolean
validate_actions (kf_validator *kf)
{
  g_hash_table_foreach_remove (kf->action_values,
                               (GHRFunc) lookup_group_foreach_action, kf);

  g_hash_table_foreach (kf->action_values,
                        (GHFunc) print_error_foreach_action, kf);

  g_hash_table_foreach (kf->action_groups,
                        (GHFunc) print_error_foreach_group, kf);

  return (g_hash_table_size (kf->action_values) +
          g_hash_table_size (kf->action_groups) == 0);
}

/* + These desktop entry files should have the extension .desktop.
 *   Checked.
 * + Desktop entries which describe how a directory is to be
 *   formatted/displayed should be simply called .directory.
 *   Checked.
 * + Using .kdelnk instead of .desktop as the file extension is deprecated.
 *   Checked.
 * FIXME: we're not doing what the spec says wrt Directory.
 */
static gboolean
validate_filename (kf_validator *kf)
{
  if (kf->type == DIRECTORY_TYPE) {
    if (g_str_has_suffix (kf->filename, ".directory"))
      return TRUE;
    else {
      print_fatal (kf, "file is of type \"Directory\", but filename does not "
                       "have a .directory extension\n");
      return FALSE;
    }
  }

  if (g_str_has_suffix (kf->filename, ".desktop"))
    return TRUE;

  if (g_str_has_suffix (kf->filename, ".kdelnk")) {
    if (kf->kde_reserved_warnings || !kf->no_deprecated_warnings)
      print_warning (kf, "filename has a .kdelnk extension, which is "
                         "deprecated in favor of .desktop\n");
    return TRUE;
  }

  print_fatal (kf, "filename does not have a .desktop extension\n");
  return FALSE;
}

gboolean
desktop_file_validate (const char *filename,
                       gboolean    warn_kde,
                       gboolean    no_warn_deprecated)
{
  GError       *error;
  kf_validator  kf;

  /* just a consistency check */
  g_assert (G_N_ELEMENTS (registered_types) == LAST_TYPE - 1);

  kf.filename               = filename;
  kf.keyfile                = g_key_file_new ();
  kf.kde_reserved_warnings  = warn_kde;
  kf.no_deprecated_warnings = no_warn_deprecated;

  error = NULL;
  if (!g_key_file_load_from_file (kf.keyfile, filename,
			  	  G_KEY_FILE_KEEP_COMMENTS|
                                  G_KEY_FILE_KEEP_TRANSLATIONS,
				  &error)) {
    print_fatal (&kf, "parse error: %s\n", error->message);
    g_error_free (error);
    g_key_file_free (kf.keyfile);

    return FALSE;
  }

  kf.main_group       = NULL;
  kf.type             = INVALID_TYPE;
  kf.type_string      = NULL;
  kf.show_in          = FALSE;
  kf.application_keys = NULL;
  kf.link_keys        = NULL;
  kf.fsdevice_keys    = NULL;
  kf.mimetype_keys    = NULL;
  kf.action_values    = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL, g_free);
  kf.action_groups    = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL, g_free);
  kf.fatal_error      = FALSE;

  validate_groups_and_keys (&kf);
  //FIXME: this does not work well if there are both a Desktop Entry and a KDE
  //Desktop Entry groups since only the last one will be validated for this.
  if (kf.main_group) {
    validate_required_keys (&kf);
    validate_type_keys (&kf);
  }
  validate_actions (&kf);
  validate_filename (&kf);

  g_list_foreach (kf.application_keys, (GFunc) g_free, NULL);
  g_list_free (kf.application_keys);
  g_list_foreach (kf.link_keys, (GFunc) g_free, NULL);
  g_list_free (kf.link_keys);
  g_list_foreach (kf.fsdevice_keys, (GFunc) g_free, NULL);
  g_list_free (kf.fsdevice_keys);
  g_list_foreach (kf.mimetype_keys, (GFunc) g_free, NULL);
  g_list_free (kf.mimetype_keys);

  g_hash_table_destroy (kf.action_values);
  g_hash_table_destroy (kf.action_groups);

  g_key_file_free (kf.keyfile);

  return (!kf.fatal_error);
}

/* return FALSE if we were unable to fix the file */
gboolean
desktop_file_fixup (GKeyFile   *kf,
                    const char *filename)
{
  char         *value;
  unsigned int  i;
  
  if (g_key_file_has_group (kf, GROUP_KDE_DESKTOP_ENTRY)) {
    g_printerr ("%s: renaming deprecated \"%s\" group to \"%s\"\n",
                filename, GROUP_KDE_DESKTOP_ENTRY, GROUP_DESKTOP_ENTRY);
    dfu_key_file_rename_group (kf,
                               GROUP_KDE_DESKTOP_ENTRY, GROUP_DESKTOP_ENTRY);
  }
  
  /* Fix lists to have a ';' at the end if they don't */
  for (i = 0; i < G_N_ELEMENTS (registered_desktop_keys); i++) {
    if (registered_desktop_keys[i].type != DESKTOP_STRING_LIST_TYPE &&
        registered_desktop_keys[i].type != DESKTOP_REGEXP_LIST_TYPE)
      continue;

    value = g_key_file_get_value (kf, GROUP_DESKTOP_ENTRY,
                                  registered_desktop_keys[i].name, NULL);
    if (value) {
      int len;

      len = strlen (value);

      if (len > 0 && (value[len - 1] != ';' ||
                      (len > 1 && value[len - 2] == '\\' &&
                       (len < 3 || value[len - 3] != '\\')))) {
          char *str;

          g_printerr ("%s: key \"%s\" is a list and does not have a "
                      "semicolon as trailing character, fixing\n",
                      filename, registered_desktop_keys[i].name);
          
          str = g_strconcat (value, ";", NULL);
          g_key_file_set_value (kf, GROUP_DESKTOP_ENTRY,
                                registered_desktop_keys[i].name, str);
          g_free (str);
      }
    }
  }

  return TRUE;
}
