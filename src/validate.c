#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "desktop_file.h"
#include "validate.h"

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x


struct KeyHashData {
  gboolean has_non_translated;
  gboolean has_translated;
};

struct KeyData {
  GHashTable *hash;
  const char *filename;
  gboolean deprecated;
};

static gboolean fatal_error_occurred = FALSE;


static void
print_fatal (const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (format != NULL);
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs (str, stdout);

  fflush (stdout);
  
  g_free (str);

  fatal_error_occurred = TRUE;
}

static void
print_warning (const char *format, ...)
{
  va_list args;
  gchar *str;
  
  g_return_if_fail (format != NULL);
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs (str, stdout);

  fflush (stdout);
  
  g_free (str);
}

static void
validate_string (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  const char *p;
  gboolean ok = TRUE;
  char *k;

  p = value;
  while (*p)
    {
      if (!(g_ascii_isprint (*p) || *p == '\n' || *p == '\t'))
	{
	  ok = FALSE;
	  break;
	}
      
      p++;
    }

  if (!ok)
    {
      if (locale)
	k = g_strdup_printf ("%s[%s]", key, locale);
      else
	k = g_strdup_printf ("%s", key);
      print_fatal ("Error in file %s, Invalid characters in value of key %s. Keys of type string may contain ASCII characters except control characters\n", filename, k);
      g_free (k);
    }
}

static void
validate_strings (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  const char *p;
  gboolean ok = TRUE;
  char *k;

  p = value;
  while (*p)
    {
      if (!(g_ascii_isprint (*p) || *p == '\n' || *p == '\t'))
	{
	  ok = FALSE;
	  break;
	}
      
      p++;
    }

  if (!ok)
    {
      if (locale)
	k = g_strdup_printf ("%s[%s]", key, locale);
      else
	k = g_strdup_printf ("%s", key);
      print_fatal ("Error in file %s, Invalid characters in value of key %s. Keys of type strings may contain ASCII characters except control characters\n", filename, k);
      g_free (k);
    }

  /* Check that we end in a semicolon */
  if (p != value)
    {
      --p;
      if (*p != ';')
        {
          if (locale)
            k = g_strdup_printf ("%s[%s]", key, locale);
          else
            k = g_strdup_printf ("%s", key);

          print_fatal ("Error in file %s, key %s is a list of strings and must end in a semicolon.\n", filename, k);
          g_free (k);
        }
    }
}

static void
validate_only_show_in (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  char **vals;
  int i;

  validate_strings (value, key, locale, filename, df);

  vals = g_strsplit (value, ";", G_MAXINT);

  i = 0;
  while (vals[i])
    ++i;

  if (i == 0)
    {
      g_strfreev (vals);
      return;
    }
  
  /* Drop the empty string g_strsplit leaves in the vector since
   * our list of strings ends in ";"
   */
  --i;
  g_free (vals[i]);
  vals[i] = NULL;

  i = 0;
  while (vals[i])
    {
      if (strcmp (vals[i], "KDE") != 0 &&
          g_ascii_strcasecmp (vals[i], "KDE") == 0)
        print_fatal ("Error in file %s, OnlyShowIn value for KDE should be all caps KDE, not %s.\n", vals[i]);
      else if (strcmp (vals[i], "GNOME") != 0 &&
               g_ascii_strcasecmp (vals[i], "GNOME") == 0)
        print_fatal ("Error in file %s, OnlyShowIn value for GNOME should be all caps GNOME, not %s.\n", vals[i]);
      
      ++i;
    }

  g_strfreev (vals);
}

static void
validate_localestring (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  char *k;
  const char *encoding;
  char *res;
  GError *error;

  if (locale)
    k = g_strdup_printf ("%s[%s]", key, locale);
  else
    k = g_strdup_printf ("%s", key);


  if (gnome_desktop_file_get_encoding (df) == GNOME_DESKTOP_FILE_ENCODING_UTF8)
    {
      if (!g_utf8_validate (value, -1, NULL))
	print_fatal ("Error, value for key %s in file %s contains invalid UTF-8 characters, even though the encoding is UTF-8.\n", k, filename);
    }
  else if (gnome_desktop_file_get_encoding (df) == GNOME_DESKTOP_FILE_ENCODING_LEGACY)
    {
      if (locale)
	{
	  encoding = desktop_file_get_encoding_for_locale (locale);

	  if (encoding)
	    {
	      error = NULL;
	      res = g_convert (value, -1,            
			       "UTF-8",
			       encoding,
			       NULL,     
			       NULL,  
			       &error);
	      if (!res && error && error->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE)
		print_fatal ("Error, value for key %s in file %s contains characters that are invalid in the %s encoding.\n", k, filename, encoding);
	      else if (!res && error && error->code == G_CONVERT_ERROR_NO_CONVERSION)
		print_warning ("Warning, encoding (%s) for key %s in file %s is not supported by iconv.\n", encoding, k, filename);
		
	      g_free (res);
	    }
	  else
	    print_fatal ("Error in file %s, no encoding specified for locale %s\n", filename, locale);
 	}
      else
	{
	  guchar *p = (guchar *)value;
	  gboolean ok = TRUE;
	  /* non-translated strings in legacy-mixed has to be ascii. */
	  while (*p)
	    {
	      if (*p > 127)
		{
		  ok = FALSE;
		  break;
		}
	      
	      p++;
	    }
	  if (!ok)
	    print_fatal ("Error in file %s, untranslated localestring key %s has non-ascii characters in its value\n", filename, key);
	}
    }

  g_free (k);
}

static void
validate_regexps (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  const char *p;
  gboolean ok = TRUE;
  char *k;

  p = value;
  while (*p)
    {
      if (!(g_ascii_isprint (*p) || *p == '\n' || *p == '\t'))
	{
	  ok = FALSE;
	  break;
	}
      
      p++;
    }

  if (!ok)
    {
      if (locale)
	k = g_strdup_printf ("%s[%s]", key, locale);
      else
	k = g_strdup_printf ("%s", key);
      print_fatal ("Error in file %s, Invalid characters in value of key %s. Keys of type regexps may contain ASCII characters except control characters\n", filename, k);
      g_free (k);
    }
}

static void
validate_boolean (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  if (strcmp (value, "true") != 0 &&
      strcmp (value, "false") != 0)
    print_fatal ("Error in file %s, Invalid characters in value of key %s. Boolean values must be \"false\" or \"true\", the value was \"%s\".\n", filename, key, value);
  
}

static void
validate_boolean_or_01 (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  if (strcmp (value, "true") != 0 &&
      strcmp (value, "false") != 0 &&
      strcmp (value, "0") != 0 &&
      strcmp (value, "1") != 0)
    print_fatal ("Error in file %s, Invalid characters in value of key %s. Boolean values must be \"false\" or \"true\", the value was \"%s\".\n", filename, key, value);

  if (strcmp (value, "0") == 0 ||
      strcmp (value, "1") == 0)
    print_warning ("Warning in file %s, boolean key %s has value %s. Boolean values should be \"false\" or \"true\", although 0 and 1 is allowed in this field for backwards compatibility.\n", filename, key, value);
}

static void
validate_numeric (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df)
{
  float d;
  int res;
  
  res = sscanf( value, "%f", &d);
  if (res == 0)
    print_fatal ("Error in file %s, numeric key %s has value %s, which doesn't look like a number.\n", filename, key, value);
}

struct {
  char *keyname;
  void (*validate_type) (const char *value, const char *key, const char *locale, const char *filename, GnomeDesktopFile *df);
  gboolean deprecated;
} key_table[] = {
  { "Encoding", validate_string },
  { "Version", validate_numeric },
  { "Name", validate_localestring },
  { "GenericName", validate_localestring },
  { "Type", validate_string },
  { "FilePattern", validate_regexps },
  { "TryExec", validate_string },
  { "NoDisplay", validate_boolean },
  { "Comment", validate_localestring },
  { "Exec", validate_string },
  { "Actions", validate_strings },
  { "Icon", validate_string },
  { "MiniIcon", validate_string, TRUE },
  { "Hidden", validate_boolean },
  { "Path", validate_string },
  { "Terminal", validate_boolean_or_01 },
  { "TerminalOptions", validate_string, /* FIXME: Should be deprecated? */ },
  { "SwallowTitle", validate_localestring },
  { "SwallowExec", validate_string }, 
  { "MimeType", validate_regexps },
  { "Patterns", validate_regexps },
  { "DefaultApp", validate_string },
  { "Dev", validate_string },
  { "FSType", validate_string },
  { "MountPoint", validate_string },
  { "ReadOnly", validate_boolean_or_01 },
  { "UnmountIcon", validate_string },
  { "SortOrder", validate_strings /* FIXME: Also comma-separated */},
  { "URL", validate_string },
  { "Categories", validate_strings }, /* FIXME: should check that each category is known */
  { "OnlyShowIn", validate_only_show_in },
  { "StartupNotify", validate_boolean },
  { "StartupWMClass", validate_string }
};

static void
enum_keys (GnomeDesktopFile *df,
	   const char       *key, /* If NULL, value is comment line */
	   const char       *locale,
	   const char       *value, /* This is raw unescaped data */
	   gpointer          user_data)
{
  struct KeyData *data = user_data;
  struct KeyHashData *hash_data;
  const char *p;
  int i;
  
  if (key == NULL)
    {
      if (!g_utf8_validate (value, -1, NULL))
	print_warning ("Warning, file %s contains non UTF-8 comments\n", data->filename);

      return;
    }

  hash_data = g_hash_table_lookup (data->hash, key);
  if (hash_data == NULL)
    {
      hash_data = g_new0 (struct KeyHashData, 1);
      g_hash_table_insert (data->hash, (char *)key, hash_data);
    }

  if (locale == NULL) {
    if (hash_data->has_non_translated)
	print_fatal ("Error, file %s contains multiple assignments of key %s\n", data->filename, key);
      
    hash_data->has_non_translated = TRUE;
  } else {
    hash_data->has_translated = TRUE;
  }

#ifdef VERIFY_CANONICAL_ENCODING_NAME
  if (locale)
    {
      const char *encoding;
      const char *canonical;
      
      encoding = strchr(locale, '.');

      if (encoding)
	{
	  encoding++;

	  canonical = get_canonical_encoding (encoding);
	  if (strcmp (encoding, canonical) != 0)
	    print_warning ("Warning in file %s, non-canonical encoding %s specified. The canonical name of the encoding is %s\n", data->filename, encoding, canonical);
	}
    }
#endif

  for (i = 0; i < (int) G_N_ELEMENTS (key_table); i++)
    {
      if (strcmp (key_table[i].keyname, key) == 0)
	break;
    }

  if (i < (int) G_N_ELEMENTS (key_table))
    {
      if (key_table[i].validate_type)
	(*key_table[i].validate_type) (value, key, locale, data->filename, df);
      if (key_table[i].deprecated)
	print_warning ("Warning, file %s contains key %s. Usage of this key is not recommended, since it has been deprecated\n", data->filename, key);      
    }
  else
    {
      if (strncmp (key, "X-", 2) != 0)
	print_warning ("Warning in file %s: nonstandard key \"%s\" lacks the \"X-\" prefix.\n", data->filename, key);
    }

  /* Validation of specific keys */

  if (strcmp (key, "Icon") == 0)
    {
#if 0
      /* With new icon theme spec we allow this */
      if (strchr (value, '.') == NULL)
	print_warning ("Warning, icon '%s' specified in file %s does not seem to contain a filename extension\n", value, data->filename);
#endif
    }
  
  if (strcmp (key, "Exec") == 0)
    {
      if (strstr (value, "NO_XALF") != NULL)
	print_fatal ("Error, The Exec string for file %s includes the nonstandard broken NO_XALF prefix\n", data->filename);

      p = value;
      while (*p)
	{
	  if (*p == '%')
	    {
	      p++;
	      if (*p != 'f' && *p != 'F' &&
		  *p != 'u' && *p != 'U' &&
		  *p != 'd' && *p != 'D' &&
		  *p != 'n' && *p != 'N' &&
		  *p != 'i' && *p != 'm' &&
		  *p != 'c' && *p != 'k' &&
		  *p != 'v' && *p != '%')
		print_fatal ("Error, The Exec string for file %s includes non-standard parameter %%%c\n", data->filename, *p);
	      if (*p == 0)
		break;
	    }
	  p++;
	}
    }

  
}


static void
enum_hash_keys (gpointer       key,
		gpointer       value,
		gpointer       user_data)
{
  struct KeyData *data = user_data;
  struct KeyHashData *hash_data = value;

  if (hash_data->has_translated &&
      !hash_data->has_non_translated)
    print_fatal ("Error in file %s, key %s is translated, but no untranslated version exists\n", data->filename, (char *)key);
  
}

static void
generic_keys (GnomeDesktopFile *df, const char *filename)
{
  struct KeyData data = {0 };
  
  data.hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  data.filename = filename;
    
  gnome_desktop_file_foreach_key (df, NULL, TRUE,
				  enum_keys, &data);

  g_hash_table_foreach (data.hash,
			enum_hash_keys,
			&data);

  g_hash_table_destroy (data.hash);
}

struct SectionData {
  GHashTable *hash;
  const char *filename;
  const char *main_section;
  gboolean has_kde_desktop_entry;
};

static void
enum_sections (GnomeDesktopFile *df,
	       const char       *name,
	       gpointer          data)
{
  struct SectionData *section = data;

  g_assert (name != NULL);

  if (strcmp (name, "Desktop Entry") == 0 ||
      strcmp (name, "KDE Desktop Entry") == 0)
    {
      if (!section->main_section)
	{
	  section->main_section = name;
	}
      else
	{
	  print_fatal ("Error, file %s already contains section %s, should not contain another section %s\n",
		       section->filename, section->main_section, name);
	}

      if (strcmp (name, "KDE Desktop Entry") == 0)
	section->has_kde_desktop_entry = TRUE;
    }
  else if (strncmp (name, "Desktop Action ", 15) != 0 &&
	   strncmp (name, "X-", 2) != 0)
    {
      print_fatal ("Error, file %s contains section %s, extensions to the spec should use section names starting with \"X-\".\n",
		   section->filename, name);
    }

  if (g_hash_table_lookup (section->hash, name))
    print_fatal ("Error, file %s contains multiple sections named %s\n", section->filename, name);
  else
    g_hash_table_insert (section->hash, (char *)name, (char *)name);
}

static const char *
required_section (GnomeDesktopFile *df, const char *filename)
{
  struct SectionData section;
  
  section.hash = g_hash_table_new (g_str_hash, g_str_equal);
  section.filename = filename;
  section.main_section = NULL;
  section.has_kde_desktop_entry = FALSE;
    
  gnome_desktop_file_foreach_section (df, enum_sections, &section);

  if (!section.main_section)
    {
      print_fatal ("Error, file %s doesn't contain a desktop entry section\n", filename);
    }
  else if (section.has_kde_desktop_entry)
    {
      print_warning ("Warning, file %s contains a \"KDE Desktop Entry\" section. This has been deprecated in favor of \"Desktop Entry\"\n",
		     filename);
    }

  g_hash_table_destroy (section.hash);
  
  return section.main_section;
}

static gboolean
required_keys (GnomeDesktopFile *df, const char *section, const char *filename)
{
  const char *val;
  
  if (gnome_desktop_file_get_raw (df, section,
				  "Encoding",
				  NULL, &val))
    {
      if (strcmp (val, "UTF-8") != 0 &&
	  strcmp (val, "Legacy-Mixed") != 0)
	print_fatal ("Error, file %s specifies unknown encoding type '%s'.\n", filename, val);
    }
  else
    {
      print_fatal ("Error, file %s does not contain the \"Encoding\" key. This is a required field for all desktop files.\n", filename);
    }

  if (!gnome_desktop_file_get_raw (df, section,
				   "Name",
				   NULL, &val))
    {
      print_fatal ("Error, file %s does not contain the \"Name\" key. This is a required field for all desktop files.\n", filename);
    }

  if (gnome_desktop_file_get_raw (df, section,
				  "Type",
				  NULL, &val))
    {
      if (strcmp (val, "Application") != 0 &&
	  strcmp (val, "Link") != 0 &&
	  strcmp (val, "FSDevice") != 0 &&
	  strcmp (val, "MimeType") != 0 &&
	  strcmp (val, "Directory") != 0 &&
	  strcmp (val, "Service") != 0 &&
	  strcmp (val, "ServiceType") != 0)
	{
	  print_fatal ("Error, file %s specifies an invalid type '%s'.\n", filename, val);
	}
    }
  else
    {
      print_fatal ("Error, file %s does not contain the \"Type\" key. This is a required field for all desktop files.\n", filename);
    }
  return TRUE;
}

struct ActionsData {
  GHashTable *hash;
  const char *filename;
};

static void
enum_actions (GnomeDesktopFile *df,
	      const char       *section,
	      gpointer          data)
{
  struct ActionsData *actions_data = data;
  const char *val;
  const char *action;

  g_assert (section != NULL);

  if (strncmp (section, "Desktop Action ", 15) != 0)
    return;

  action = section + 15;

  /* Already verified this */
  g_assert (!g_hash_table_lookup (actions_data->hash, action));

  g_hash_table_insert (actions_data->hash, (char *) action, (char *) action);

  if (!gnome_desktop_file_get_raw (df, section,
				   "Exec",
				   NULL, &val))
    {
      print_fatal ("Error, file %s contains \"Desktop Action %s\" section which lacks an Exec key.\n",
		   actions_data->filename, section);
    }
}

static void
error_orphaned_action (gpointer       key,
		       gpointer       value,
		       gpointer       user_data)
{
  const char *action = key;
  const char *filename = user_data;

  print_fatal ("Error, file %s contains \"Desktop Action %s\" but Actions key does not contain '%s'\n",
	       filename, action, action);
}

static gboolean
required_actions (GnomeDesktopFile *df, const char *filename)
{
  struct ActionsData actions_data;
  gboolean retval = FALSE;

  actions_data.hash = g_hash_table_new (g_str_hash, g_str_equal);
  actions_data.filename = filename;
    
  gnome_desktop_file_foreach_section (df, enum_actions, &actions_data);

  if (g_hash_table_size (actions_data.hash) > 0)
    {
      const char *val;
      char **actions;
      int i;

      if (!gnome_desktop_file_get_raw (df, NULL, "Actions", NULL, &val))
	{
	  print_fatal ("Error, file %s has \"Desktop Action\" sections but no Action key.\n", filename);
	  goto out;
	}

      actions = g_strsplit (val, ";", G_MAXINT);
      for (i = 0; actions [i]; i++)
	{
	  if (*actions [i] == '\0')
	    continue;

	  if (!g_hash_table_lookup (actions_data.hash, actions [i]))
	    {
	      print_fatal ("Error, Action key contains '%s' but file %s has \"Desktop Action %s\" section.\n",
			   actions [i], filename, actions [i]);
	      goto out;
	    }

	  g_hash_table_remove (actions_data.hash, actions [i]);
	}

      g_strfreev (actions);

      if (g_hash_table_size (actions_data.hash) > 0)
	{
	  g_hash_table_foreach (actions_data.hash,
				error_orphaned_action,
				(char *) filename);
	  goto out;
	}
    }

  retval = TRUE;

 out:
  g_hash_table_destroy (actions_data.hash);

  return retval;
}

gboolean
desktop_file_validate (GnomeDesktopFile *df, const char *filename)
{
  const char *name;
  const char *comment;
  const char *main_section;

  /* FIXME global variable cruft */
  fatal_error_occurred = FALSE;
  
  if ((main_section = required_section (df, filename)) == NULL)
    return !fatal_error_occurred;
  if (!required_keys (df, main_section, filename))
    return !fatal_error_occurred;
  if (!required_actions (df, filename))
    return !fatal_error_occurred;

  generic_keys (df, filename);

  if (gnome_desktop_file_get_raw (df, NULL, "Name", NULL, &name) &&
      gnome_desktop_file_get_raw (df, NULL, "Comment", NULL, &comment))
    {
      if (strcmp (name, comment) == 0)
	print_warning ("Warning in file %s, the fields Name and Comment have the same value\n", filename);
    }

  return !fatal_error_occurred;
}

/* return FALSE if we were unable to fix the file */
gboolean
desktop_file_fixup (GnomeDesktopFile *df,
                    const char       *filename)
{
  const char *val;
  gboolean fix_encoding;
  const char *string_list_keys[] = { "Actions", "SortOrder", "Categories" };
  int i;
  
  if (gnome_desktop_file_has_section (df, "KDE Desktop Entry"))
    {
      g_printerr (_("Changing deprecated [KDE Desktop Entry] to plain [Desktop Entry]\n"));
      gnome_desktop_file_rename_section (df,
                                         "KDE Desktop Entry",
                                         "Desktop Entry");
    }
  
  fix_encoding = FALSE;  
  
  if (gnome_desktop_file_get_raw (df, NULL,
				  "Encoding",
				  NULL, &val))
    {
      if (strcmp (val, "UTF-8") != 0 &&
	  strcmp (val, "Legacy-Mixed") != 0)
        {
          g_printerr (_("File \"%s\" has bogus encoding \"%s\" "),
                      filename, val);
          fix_encoding = TRUE;
        }
    }
  else
    {
      g_printerr (_("File \"%s\" has missing encoding "),
                  filename);
      fix_encoding = TRUE;
    }

  if (fix_encoding)
    {
      /* If Encoding was missing or bogus, the desktop file parser guessed */
      switch (gnome_desktop_file_get_encoding (df))
        {
        case GNOME_DESKTOP_FILE_ENCODING_LEGACY:
          g_printerr (_(" (guessed Legacy-Mixed)\n"));
          gnome_desktop_file_set_raw (df, NULL, "Encoding", NULL, "Legacy-Mixed");
          break;
        case GNOME_DESKTOP_FILE_ENCODING_UTF8:
          g_printerr (_(" (guessed UTF-8)\n"));
          gnome_desktop_file_set_raw (df, NULL, "Encoding", NULL, "UTF-8");
          break;          
        case GNOME_DESKTOP_FILE_ENCODING_UNKNOWN:
          g_printerr (_("\nNot enough data to guess at encoding of \"%s\"!\n"),
                      filename);
          return FALSE;
          break;
        }
    }

  /* Fix string lists to have a ';' at the end if they don't */
  i = 0;
  while (i < (int) G_N_ELEMENTS (string_list_keys))
    {
      if (gnome_desktop_file_get_raw (df, NULL,
                                      string_list_keys[i],
                                      NULL, &val))
        {
          int len;

          len = strlen (val);

          if (len > 0 && val[len-1] != ';')
            {
              char *str;

              g_printerr ("File \"%s\" key \"%s\" string list not semicolon-terminated, fixing\n",
                          filename, string_list_keys[i]);
              
              str = g_strconcat (val, ";", NULL);
              gnome_desktop_file_set_raw (df, NULL,
                                          string_list_keys[i],
                                          NULL, str);
              g_free (str);
            }
        }
      
      ++i;
    }
  
  return TRUE;
}
