/* Copyright © 2013 Canonical Limited
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
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "utils.h"

gboolean
write_symlinked_file (const gchar  *applications_dir,
                      const gchar  *basename,
                      const gchar  *contents,
                      GError      **error)
{
  gboolean success = FALSE;
  gchar *linkname;
  gchar *filename;
  gchar *expected_link_content;
  gchar *link_content;
  gchar *dirname;

  dirname = g_build_filename (dir, ".metadata", NULL);
  linkname = g_build_filename (dir, basename, NULL);
  filename = g_build_filename (dir, ".metadata", basename, NULL);
  expected_link_content = g_build_filename (".metadata", basename, NULL);

  link_content = g_file_read_link (linkname, NULL);
  if (!link_content || !g_str_equal (link_content, expected_link_content))
    {
      /* Ignore failures to unlink -- the file may simply not exist.
       * Any other errors will be caught when we actually try to replace
       * it...
       */
      unlink (linkname);

      if (symlink (expected_link_content, linkname) != 0)
        {
          gint saved_errno = errno;

          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                       "Unable to create symbolic link %s: %s", linkname, g_strerror (saved_errno));
          goto out;
        }
    }

  /* OK.  The link should be setup properly now.
   *
   * Again, ignore any errors here -- we can report them when
   * g_file_set_contents() fails.
   */
  mkdir (dirname, 0777);

  success = g_file_set_contents (filename, content, -1, error);

out:
  g_free (dirname);
  g_free (linkname);
  g_free (filename);
  g_free (expected_link_content);
  g_free (link_content);

  return success;
}
