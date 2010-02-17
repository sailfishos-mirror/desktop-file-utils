/* mimeutils.c: useful functions related to mime types
 * vim: set ts=2 sw=2 et: */

/*
 * Copyright (C) 2004  Red Hat, Inc.
 * Copyright (C) 2008  Novell, Inc.
 *
 * Written by Vincent Untz <vuntz@gnome.org>, based on code from
 * update-desktop-base.c which was originally written by
 * Ray Strode <rstrode@redhat.com>
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

/* Related documentation:
 *   + Registered media types: http://www.iana.org/assignments/media-types/
 *   + RFC about MIME, with the BNF grammar: http://tools.ietf.org/html/rfc2045
 *   + RFC about media types: http://tools.ietf.org/html/rfc2046
 *   + RFC about the model media type: http://tools.ietf.org/html/rfc2077
 */

#include <string.h>

#include <glib.h>

#include "mimeutils.h"

/* Defined in RFC 2045/2046 and RFC 2077 */
static const char *registered_discrete_media_types[] = {
  "application", "audio", "image", "model", "text", "video"
};

/* Defined in RFC 2045/2046 */
static const char *registered_composite_media_types[] = {
  "message", "multipart"
};

static const char *registered_not_used_media_types[] = {
  "example"
};

/* TODO: we will break in a few cases, since we're only IANA-aware so far.
 * Known cases:
 *  inode/.*: added by the fdo spec
 *  flv-application/octet-stream: alias in the fdo db
 *  zz-application/zz-winassoc-cdr: alias in the fdo db
 *  misc/ultravox: widely used?
 *
 * TODO: might actually be nice to download at distcheck time all the
 * registered subtypes and warn when using a non-registered non-experimental
 * subtype.
 */

/* From the BNF grammar:
 *
 *  token := 1*<any (US-ASCII) CHAR except SPACE, CTLs,
 *              or tspecials>
 *
 *  tspecials :=  "(" / ")" / "<" / ">" / "@" /
 *                "," / ";" / ":" / "\" / <">
 *                "/" / "[" / "]" / "?" / "="
 */
static gboolean
is_valid_mime_type_char (const guchar c)
{
  char invalid_chars[] = "()<>@,;:\\\"/[]?=";

  /* Filter out control chars and space */
  if ((c <= 32) || (c == 127))
    return FALSE;

  if (memchr (invalid_chars, c, sizeof (invalid_chars)) != NULL)
    return FALSE;

  return TRUE;
}

/* From the BNF grammar:
 *
 *    x-token := <The two characters "X-" or "x-" followed, with
 *                no  intervening white space, by any token>
 *
 * From RFC 2046:
 *   In general, the use of "X-" top-level types is strongly discouraged.
 *   Implementors should invent subtypes of the existing types whenever
 *   possible. In many cases, a subtype of "application" will be more
 *   appropriate than a new top-level type.
 */
static MimeUtilsValidity
is_valid_media_type (const char  *media_type,
                     char       **error)
{
  unsigned int i;

  if (g_ascii_strncasecmp (media_type, "X-", 2) == 0) {
    for (i = 2; media_type[i]; i++) {
      if (!is_valid_mime_type_char (media_type[i]))
        if (error)
          *error = g_strdup_printf ("\"%s\" a media type that contains "
                                    "an invalid character", media_type);
        return FALSE;
    }

    if (error)
      *error = g_strdup_printf ("the use of \"%s\" as media type is strongly "
                                "discouraged in favor of a subtype of the "
                                "\"application\" media type", media_type);

    return MU_DISCOURAGED;
  }

#define IF_IS_MEDIA_TYPE_IN(list)             \
  for (i = 0; i < G_N_ELEMENTS (list); i++) { \
    if (strcmp (media_type, list[i]) == 0)       \
      break;                                  \
  }                                           \
  if (i < G_N_ELEMENTS (list))

  IF_IS_MEDIA_TYPE_IN (registered_discrete_media_types)
    return MU_VALID;

  IF_IS_MEDIA_TYPE_IN (registered_composite_media_types) {
    if (error)
      *error = g_strdup_printf ("\"%s\" is a media type that probably does "
                                "not make sense in this context", media_type);
    return MU_DISCOURAGED;
  }

  IF_IS_MEDIA_TYPE_IN (registered_not_used_media_types) {
    if (error)
      *error = g_strdup_printf ("\"%s\" is a media type that must not "
                                "be used", media_type);
    return MU_INVALID;
  }

  if (error)
    *error = g_strdup_printf ("\"%s\" is an unregistered media type",
                              media_type);

  return MU_INVALID;
}

MimeUtilsValidity
mu_mime_type_is_valid (const char  *mime_type,
                       char       **error)
{
  char *media_type;
  char *subtype;
  MimeUtilsValidity media_type_validity;

  media_type = g_strdup (mime_type);
  subtype = strchr (media_type, '/');

  if (!subtype) {
    if (error)
      *error = g_strdup_printf ("\"%s\" does not contain a subtype",
                                mime_type);
    g_free (media_type);
    return MU_INVALID;
  }

  subtype[0] = '\0';
  subtype++;

  if (subtype[0] == '\0') {
    if (error)
      *error = g_strdup_printf ("\"%s\" contains an empty subtype",
                                mime_type);
    g_free (media_type);
    return MU_INVALID;
  }

  for (; subtype[0] != '\0'; subtype++) {
      if (!is_valid_mime_type_char (subtype[0])) {
        if (error)
          *error = g_strdup_printf ("\"%s\" contains an invalid character in "
                                    "the subtype", mime_type);

        g_free (media_type);
        return MU_INVALID;
      }
  }

  media_type_validity = is_valid_media_type (media_type, error);
  g_free (media_type);

  return media_type_validity;
}
