/*
 *    This file is part of darktable,
 *    copyright (c) 2017--2018 tobias ellinghaus.
 *
 *    darktable is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    darktable is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/l10n.h"
#include "control/conf.h"

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#ifdef _WIN32
#include "win/dtwin.h"
#include <windows.h>
#endif

static void set_locale(const char *ui_lang, const char *old_env)
{
  if(ui_lang && *ui_lang)
  {
    // TODO: Also set LANG
    g_setenv("LANGUAGE", ui_lang, TRUE);
    gtk_disable_setlocale();
  }
  else if(old_env && *old_env)
    g_setenv("LANGUAGE", old_env, TRUE);
  else
    g_unsetenv("LANGUAGE");

  setlocale(LC_ALL, "");
}

static gint sort_languages(gconstpointer a, gconstpointer b)
{
  gchar *name_a = g_utf8_casefold(dt_l10n_get_name((const dt_l10n_language_t *)a), -1);
  gchar *name_b = g_utf8_casefold(dt_l10n_get_name((const dt_l10n_language_t *)b), -1);

  int result = g_strcmp0(name_a, name_b);

  g_free(name_a);
  g_free(name_b);

  return result;
}

static void get_language_names(GList *languages)
{
#ifdef HAVE_ISO_CODES

  JsonReader *reader = NULL;

#ifdef _WIN32 // TODO: add osx?
  char datadir[PATH_MAX] = { 0 };
  dt_loc_get_datadir(datadir, sizeof(datadir));
  char *filename = g_build_filename(datadir, "..",  "iso-codes", "json", "iso_639-2.json", NULL);
  // on windows we are shipping the translations of iso-codes along ours
  char *localedir = g_build_filename(datadir, "..", "locale", NULL);
  bindtextdomain("iso_639", localedir);
  g_free(localedir);
#else
  char *filename = g_build_filename(ISO_CODES_LOCATION, "iso_639-2.json", NULL);
  bindtextdomain("iso_639", ISO_CODES_LOCALEDIR);
#endif

  bind_textdomain_codeset("iso_639", "UTF-8");

  GError *error = NULL;
  JsonParser *parser = json_parser_new();
  if(!json_parser_load_from_file(parser, filename, &error))
  {
    fprintf(stderr, "[l10n] error: parsing json from `%s' failed\n%s\n", filename, error->message);
    goto end;
  }

  // go over the json
  JsonNode *root = json_parser_get_root(parser);
  if(!root)
  {
    fprintf(stderr, "[l10n] error: can't get root node of `%s'\n", filename);
    goto end;
  }

  reader = json_reader_new(root);

  if(!json_reader_read_member(reader, "639-2"))
  {
    fprintf(stderr, "[l10n] error: unexpected layout of `%s'\n", filename);
    goto end;
  }

  if(!json_reader_is_array(reader))
  {
    fprintf(stderr, "[l10n] error: unexpected layout of `%s'\n", filename);
    goto end;
  }

  int n_elements = json_reader_count_elements(reader);
  for(int i = 0; i < n_elements; i++)
  {
    json_reader_read_element(reader, i);
    if(!json_reader_is_object(reader))
    {
      fprintf(stderr, "[l10n] error: unexpected layout of `%s' (element %d)\n", filename, i);
      goto end;
    }

    const char *alpha_2 = NULL, *alpha_3 = NULL, *name = NULL;
    if(json_reader_read_member(reader, "alpha_2"))
      alpha_2 = json_reader_get_string_value(reader);
    json_reader_end_member(reader); // alpha_2

    if(json_reader_read_member(reader, "alpha_3"))
      alpha_3 = json_reader_get_string_value(reader);
    json_reader_end_member(reader); // alpha_3

    if(json_reader_read_member(reader, "name"))
      name = json_reader_get_string_value(reader);
    json_reader_end_member(reader); // name

    if(name && (alpha_2 || alpha_3))
    {
      // check if alpha_2 or alpha_3 is in our translations
      for(GList *iter = languages; iter; iter = g_list_next(iter))
      {
        dt_l10n_language_t *language = (dt_l10n_language_t *)iter->data;
        if(!g_strcmp0(language->base_code, alpha_2) || !g_strcmp0(language->base_code, alpha_3))
        {
          // code taken in parts from GIMP's gimplanguagestore-parser.c
          g_setenv("LANGUAGE", language->code, TRUE);
          setlocale (LC_ALL, language->code);

          char *localized_name = g_strdup(dgettext("iso_639", name));

          /* If original and localized names are the same for other than English,
           * maybe localization failed. Try now in the main dialect. */
          if(g_strcmp0(name, localized_name) == 0 &&
             g_strcmp0(language->code, language->base_code) != 0)
          {
            g_free(localized_name);

            g_setenv("LANGUAGE", language->base_code, TRUE);
            setlocale (LC_ALL, language->base_code);

            localized_name = g_strdup(dgettext("iso_639", name));
          }

          /*  there might be several language names; use the first one  */
          char *semicolon = strchr(localized_name, ';');

          if(semicolon)
          {
            char *tmp = localized_name;
            localized_name = g_strndup(localized_name, semicolon - localized_name);
            g_free(tmp);
          }

          // we initialize the name to the language code to have something on systems lacking iso-codes, so free it!
          g_free(language->name);
          language->name = g_strdup_printf("%s (%s)%s", localized_name, language->code, language->is_default ? " *" : "");
          g_free(localized_name);

          // we can't break out of the loop here. at least pt is in our list twice!
        }
      }
    }
    else
      fprintf(stderr, "[l10n] error: element %d has no name, skipping\n", i);

    json_reader_end_element(reader);
  }

  json_reader_end_member(reader); // 639-2

end:
  // cleanup
  g_free(filename);
  if(error) g_error_free(error);
  if(reader) g_object_unref(reader);
  if(parser) g_object_unref(parser);

#endif // HAVE_ISO_CODES
}

dt_l10n_t *dt_l10n_init(gboolean init_list)
{
  dt_l10n_t *result = (dt_l10n_t *)calloc(1, sizeof(dt_l10n_t));
  result->selected = -1;
  result->sys_default = -1;

  char *ui_lang = dt_conf_get_string("ui_last/gui_language");
  const char *old_env = g_getenv("LANGUAGE");

#if defined(_WIN32)
  // get the default locale if no language preference was specified in the config file
  if(!ui_lang || !*ui_lang)
  {
    const wchar_t *wcLocaleName = NULL;
    wcLocaleName = dtwin_get_locale();
    if(wcLocaleName != NULL)
    {
      gchar *langLocale;
      langLocale = g_utf16_to_utf8(wcLocaleName, -1, NULL, NULL, NULL);
      if(langLocale != NULL)
      {
        g_free(ui_lang);
        ui_lang = g_strdup(langLocale);
      }
    }
  }
#endif // defined (_WIN32)


  // prepare the list of available gui translations from which the user can pick in prefs
  if(init_list)
  {
    dt_l10n_language_t *selected = NULL;
    dt_l10n_language_t *sys_default = NULL;

    dt_l10n_language_t *language = (dt_l10n_language_t *)calloc(1, sizeof(dt_l10n_language_t));
    language->code = g_strdup("C");
    language->base_code = g_strdup("C");
    language->name = g_strdup("English");
    result->languages = g_list_append(result->languages, language);

    if(g_strcmp0(ui_lang, "C") == 0) selected = language;

    const gchar * const * default_languages = g_get_language_names();

#ifdef _WIN32
    char datadir[PATH_MAX] = { 0 };
    dt_loc_get_datadir(datadir, sizeof(datadir));
    char *localedir = g_build_filename(datadir, "..", "locale", NULL);
#else
    char * localedir = g_strdup(DARKTABLE_LOCALEDIR);
#endif
    GDir *dir = g_dir_open(localedir, 0, NULL);
    if(dir)
    {
      const gchar *locale;
      while((locale = g_dir_read_name(dir)))
      {
        gchar *testname = g_build_filename(localedir, locale, "LC_MESSAGES", GETTEXT_PACKAGE ".mo", NULL);
        if(g_file_test(testname, G_FILE_TEST_EXISTS))
        {
          language = (dt_l10n_language_t *)calloc(1, sizeof(dt_l10n_language_t));
          result->languages = g_list_prepend(result->languages, language);

          // some languages have a regional part in the filename, we don't want that for name lookup
          char *delimiter = strchr(locale, '_');
          if(delimiter)
            language->base_code = g_strndup(locale, delimiter - locale);
          else
            language->base_code = g_strdup(locale);
          delimiter = strchr(language->base_code, '@');
          if(delimiter)
          {
            char *tmp = language->base_code;
            language->base_code = g_strndup(language->base_code, delimiter - language->base_code);
            g_free(tmp);
          }

          // check if this is the system default
          if(sys_default == NULL)
          {
            for(const gchar * const * iter = default_languages; *iter; iter++)
            {
              if(g_strcmp0(*iter, locale) == 0)
              {
                language->is_default = TRUE;
                sys_default = language;
                break;
              }
            }
          }

          language->code = g_strdup(locale);
          language->name = g_strdup_printf("%s%s", locale, language->is_default ? " *" : "");

          if(g_strcmp0(ui_lang, language->code) == 0)
            selected = language;
        }
        g_free(testname);
      }
      g_dir_close(dir) ;
    }
    else
      fprintf(stderr, "[l10n] error: can't open directory `%s'\n", localedir);

    // now try to find language names and translations!
    get_language_names(result->languages);

    // set the requested gui language.
    // this has to happen before sorting the list as the sort result may depend on the language.
    set_locale(ui_lang, old_env);

    // sort the list of languages
    result->languages = g_list_sort(result->languages, sort_languages);

    // find the index of the selected and default languages
    int i = 0;
    for(GList *iter = result->languages; iter; iter = g_list_next(iter))
    {
      if(iter->data == sys_default) result->sys_default = i;
      if(iter->data == selected) result->selected = i;
      i++;
    }

    if(selected == NULL)
      result->selected = result->sys_default;

    g_free(localedir);
  }
  else
    set_locale(ui_lang, old_env);

  g_free(ui_lang);

  return result;
}

const char *dt_l10n_get_name(const dt_l10n_language_t *language)
{
  if(!language) return NULL;

  return language->name ? language->name : language->code;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
