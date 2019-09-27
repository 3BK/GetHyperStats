 MsgGetState();

   /* All message strings must be prefixed by the message ID. */
   ASSERT(domain != NULL);
   ASSERT(msgid != NULL);
   ASSERT(MsgHasMsgID(msgid));
#if defined(_WIN32)
   ASSERT(encoding == STRING_ENCODING_UTF8 ||
          encoding == STRING_ENCODING_UTF16_LE);
#else
   ASSERT(encoding == STRING_ENCODING_UTF8);
#endif

   /*
 *     * Find the beginning of the ID (idp) and the string (strp).
 *         * The string should have the correct MSG_MAGIC(...)... form.
 *             */

   idp = msgid + MSG_MAGIC_LEN + 1;
   strp = strchr(idp, ')') + 1;

   len = strp - idp - 1;
   ASSERT_NOT_IMPLEMENTED(len <= MSG_MAX_ID - 1);
   memcpy(idBuf, idp, len);
   idBuf[len] = '\0';

   /*
 *     * This lock is pretty coarse-grained, but a lot of the code below just runs
 *         * in exceptional situations, so it should be OK.
 *             */
   g_mutex_lock(&state->lock);

   catalog = MsgGetCatalog(domain);
   if (catalog != NULL) {
      switch (encoding) {
      case STRING_ENCODING_UTF8:
         source = catalog->utf8;
         break;

#if defined(_WIN32)
      case STRING_ENCODING_UTF16_LE:
         source = catalog->utf16;
         break;
#endif

      default:
         NOT_IMPLEMENTED();
      }
   }

#if defined(_WIN32)
   /*
 *     * Lazily create the local and UTF-16 dictionaries. This may require also
 *         * registering an empty message catalog for the desired domain.
 *             */
   if (source == NULL && encoding == STRING_ENCODING_UTF16_LE) {
      catalog = MsgGetCatalog(domain);
      if (catalog == NULL) {
         if (domain == NULL) {
            g_error("Application did not set up a default text domain.");
         }
         catalog = g_new0(MsgCatalog, 1);
         MsgSetCatalog(domain, catalog);
      }

      catalog->utf16 = HashTable_Alloc(8, HASH_STRING_KEY, g_free);
      ASSERT_MEM_ALLOC(catalog->utf16);
      source = catalog->utf16;
   }
#endif

   /*
 *     * Look up the localized string, converting to requested encoding as needed.
 *         */

   if (source != NULL) {
      const void *retval = NULL;

      if (HashTable_Lookup(source, idBuf, (void **) &retval)) {
         strp = retval;
#if defined(_WIN32)
      } else if (encoding == STRING_ENCODING_UTF16_LE) {
         gchar *converted;
         Bool success;

         /*
 *           * Look up the string in UTF-8, convert it and cache it.
 *                     */
         retval = MsgGetString(domain, msgid, STRING_ENCODING_UTF8);
         ASSERT(retval);

         converted = (gchar *) g_utf8_to_utf16(retval, -1, NULL, NULL, NULL);
         ASSERT(converted != NULL);

         success = HashTable_Insert(source, idBuf, converted);
         ASSERT(success);
         strp = converted;
#endif
      }
   }

   g_mutex_unlock(&state->lock);

   return strp;
}


/*
 *  ******************************************************************************
 *   * MsgUnescape --                                                       */ /**
 *
 *  * Does some more unescaping on top of what Escape_Undo() already does. This
 *   * function will replace '\\', '\n', '\t' and '\r' with their corresponding
 *    * characters. Substitution is done in place.
 *     *
 *      * @param[in] msg   Message to be processed.
 *       *
 *        ******************************************************************************
 *         */

static void
MsgUnescape(char *msg)
{
   char *c = msg;
   gboolean escaped = FALSE;
   size_t len = strlen(msg) + 1;

   for (; *c != '\0'; c++, len--) {
      if (escaped) {
         char subst = '\0';

         switch (*c) {
         case '\\':
            subst = '\\';
            break;

         case 'n':
            subst = '\n';
            break;

         case 'r':
            subst = '\r';
            break;

         case 't':
            subst = '\t';
            break;

         default:
            break;
         }

         if (subst != '\0') {
            *(c - 1) = subst;
            memmove(c, c + 1, len);
            c--;
         }

         escaped = FALSE;
      } else if (*c == '\\') {
         escaped = TRUE;
      }
   }
}


/*
 *  ******************************************************************************
 *   * MsgLoadCatalog --                                                    */ /**
 *
 *  * Loads the message catalog at the given path into a new hash table.
 *   *
 *    * This function supports an "extended" format for the catalog files. Aside
 *     * from the usual things you can put in a lib/dict-based dictionary, this code
 *      * supports multi-line messages so that long messages can be broken down.
 *       *
 *        * These lines are any lines following a key / value declaration that start with
 *         * a quote character (ignoring any leading spaces and tabs). So a long message
 *          * could look like this:
 *           *
 *            * @code
 *             * message.id = "This is the first part of the message. "
 *              *              "This is the continuation line, still part of the same message."
 *               * @endcode
 *                *
 *                 * The complete value for the "message.id" key will be the concatenation of
 *                  * the values in quotes.
 *                   *
 *                    * @param[in] path    Path containing the message catalog (encoding should be
 *                     *                    UTF-8).
 *                      *
 *                       * @return A new message catalog on success, NULL otherwise.
 *                        *
 *                         ******************************************************************************
 *                          */

static MsgCatalog *
MsgLoadCatalog(const char *path)
{
   gchar *localPath;
   GError *err = NULL;
   GIOChannel *stream;
   gboolean error = FALSE;
   MsgCatalog *catalog = NULL;
   HashTable *dict;

   ASSERT(path != NULL);
   localPath = VMTOOLS_GET_FILENAME_LOCAL(path, NULL);
   ASSERT(localPath != NULL);

   stream = g_io_channel_new_file(localPath, "r", &err);
   VMTOOLS_RELEASE_FILENAME_LOCAL(localPath);

   if (err != NULL) {
      g_debug("Unable to open '%s': %s\n", path, err->message);
      g_clear_error(&err);
      return NULL;
   }

   dict = HashTable_Alloc(8, HASH_STRING_KEY | HASH_FLAG_COPYKEY, g_free);
   ASSERT_MEM_ALLOC(dict);

   for (;;) {
      gboolean eof = FALSE;
      char *name = NULL;
      char *value = NULL;
      gchar *line;

      /* Read the next key / value pair. */
      for (;;) {
         gsize i;
         gsize len;
         gsize term;
         char *unused = NULL;
         gboolean cont = FALSE;

         g_io_channel_read_line(stream, &line, &len, &term, &err);

         if (err != NULL) {
            g_warning("Unable to read a line from '%s': %s\n",
                      path, err->message);
            g_clear_error(&err);
            error = TRUE;
            g_free(line);
            break;
         }

         if (line == NULL) {
            eof = TRUE;
            break;
         }

         /*
 *           * Fix the line break to always be Unix-style, to make lib/dict
 *                     * happy.
 *                               */
         if (line[term] == '\r') {
            line[term] = '\n';
            if (len > term) {
               line[term + 1] = '\0';
            }
         }

         /*
 *           * If currently name is not NULL, then check if this is a continuation
 *                     * line and, if it is, just append the contents to the current value.
 *                               */
         if (term > 0 && name != NULL && line[term - 1] == '"') {
            for (i = 0; i < len; i++) {
               if (line[i] == '"') {
                  /* OK, looks like a continuation line. */
                  char *tmp;
                  char *unescaped;

                  line[term - 1] = '\0';
                  unescaped = Escape_Undo('|', line + i + 1, len - i, NULL);
                  tmp = Str_Asprintf(NULL, "%s%s", value, unescaped);
                  free(unescaped);
                  free(value);
                  value = tmp;
                  cont = TRUE;
                  break;
               } else if (line[i] != ' ' && line[i] != '\t') {
                  break;
               }
            }
         }

         /*
 *           * If not a continuation line and we have a name, break out of the
 *                     * inner loop to update the dictionary.
 *                               */
         if (!cont && name != NULL) {
            g_free(line);
            break;
         }

         /*
 *           * Finally, try to parse the string using the dictionary library.
 *                     */
         if (!cont && DictLL_UnmarshalLine(line, len, &unused, &name, &value) == NULL) {
            g_warning("Couldn't parse line from catalog: %s", line);
            error = TRUE;
         }

         g_free(line);
         free(unused);
      }

      if (error) {
         free(name);
         free(value);
         break;
      }

      if (name != NULL) {
         ASSERT(value);

         if (!Unicode_IsBufferValid(name, strlen(name) + 1, STRING_ENCODING_UTF8) ||
             !Unicode_IsBufferValid(value, strlen(value) + 1, STRING_ENCODING_UTF8)) {
            g_warning("Invalid UTF-8 string in message catalog (key = %s)\n", name);
            error = TRUE;
            free(name);
            free(value);
            break;
         }

         MsgUnescape(value);
         HashTable_ReplaceOrInsert(dict, name, g_strdup(value));
         free(name);
         free(value);
      }

      if (eof) {
         break;
      }
   }

   g_io_channel_unref(stream);

   if (error) {
      HashTable_Free(dict);
      dict = NULL;
   } else {
      catalog = g_new0(MsgCatalog, 1);
      catalog->utf8 = dict;
   }

   return catalog;
}


/*
 *  ******************************************************************************
 *   * VMToolsMsgCleanup --                                                 */ /**
 *
 *  * Cleanup internal state, freeing up any used memory. After calling this
 *   * function, it's not safe to call any of the API exposed by this file, so
 *    * this is only called internally when the library is being unloaded.
 *     *
 *      ******************************************************************************
 *       */

void
VMToolsMsgCleanup(void)
{
   if (gMsgState != NULL) {
      if (gMsgState->domains != NULL) {
         HashTable_Free(gMsgState->domains);
      }
      g_mutex_clear(&gMsgState->lock);
      g_free(gMsgState);
   }
}


/*
 *  ******************************************************************************
 *   * VMTools_BindTextDomain --                                            */ /**
 *
 *  * Loads the message catalog for a text domain. Each text domain contains a
 *   * different set of messages loaded from a different catalog.
 *    *
 *     * If a catalog has already been bound to the given name, it is replaced with
 *      * the newly loaded data.
 *       *
 *        * @param[in] domain   Name of the text domain being loaded.
 *         * @param[in] lang     Language code for the text domain.
 *          * @param[in] catdir   Root directory of catalog files (NULL = default).
 *           *
 *            ******************************************************************************
 *             */

void
VMTools_BindTextDomain(const char *domain,
                       const char *lang,
                       const char *catdir)
{
   char *dfltdir = NULL;
   gchar *file;
   gchar *usrlang = NULL;
   MsgState *state = MsgGetState();
   MsgCatalog *catalog;

   ASSERT(domain);

   /*
 *     * If the caller has asked for the default user language, detect it and
 *         * translate to our internal language string representation.
 *             */

   if (lang == NULL || *lang == '\0') {
      usrlang = MsgGetUserLanguage();
      lang = usrlang;
   }

   g_debug("%s: user locale=%s\n", __FUNCTION__, lang);

   /*
 *     * Use the default install directory if none is provided.
 *         */

   if (catdir == NULL) {
#if defined(OPEN_VM_TOOLS)
      dfltdir = Util_SafeStrdup(VMTOOLS_DATA_DIR);
#else
      dfltdir = GuestApp_GetInstallPath();
#endif
      catdir = (dfltdir) ? dfltdir : ".";
   }

   file = g_strdup_printf("%s%smessages%s%s%s%s.vmsg",
                          catdir, DIRSEPS, DIRSEPS, lang, DIRSEPS, domain);

   if (!File_IsFile(file)) {
      /*
 *        * If we couldn't find the catalog file for the user's language, see if
 *               * we can find a more generic language (e.g., for "en_US", also try "en").
 *                      */
      char *sep = Str_Strrchr(lang, '_');
      if (sep != NULL) {
         if (usrlang == NULL) {
            usrlang = Util_SafeStrdup(lang);
         }
         usrlang[sep - lang] = '\0';
         g_free(file);
         file = g_strdup_printf("%s%smessages%s%s%s%s.vmsg",
                                catdir, DIRSEPS, DIRSEPS, usrlang, DIRSEPS, domain);
      }
   }

   catalog = MsgLoadCatalog(file);

   if (catalog == NULL) {
      if (Str_Strncmp(lang, "en", 2)) {
         /*
 *           * Don't warn about english dictionary, which may not exist (it is the
 *                     * default translation).
 *                               */
         g_message("Cannot load message catalog for domain '%s', language '%s', "
                   "catalog dir '%s'.\n", domain, lang, catdir);
      }
   } else {
      g_mutex_lock(&state->lock);
      MsgSetCatalog(domain, catalog);
      g_mutex_unlock(&state->lock);
   }
   g_free(file);
   free(dfltdir);
   g_free(usrlang);
}


/*
 *  ******************************************************************************
 *   * VMTools_GetString --                                                 */ /**
 *
 *  * Returns a localized version of the requested string in UTF-8.
 *   *
 *    * @param[in] domain    Text domain.
 *     * @param[in] msgid     Message id (including English translation).
 *      *
 *       * @return The localized string.
 *        *
 *         ******************************************************************************
 *          */

const char *
VMTools_GetString(const char *domain,
                  const char *msgid)
{
   return MsgGetString(domain, msgid, STRING_ENCODING_UTF8);
}


#if defined(_WIN32)
/*
 *  ******************************************************************************
 *   * VMTools_GetUtf16String --                                            */ /**
 *
 *  * Returns a localized string in UTF-16LE encoding. Win32 only.
 *   *
 *    * @param[in] domain    Text domain.
 *     * @param[in] msgid     Message id (including English translation).
 *      *
 *       * @return The localized string.
 *        *
 *         ******************************************************************************
 *          */

const wchar_t *
VMTools_GetUtf16String(const char *domain,
                       const char *msgid)
{
   return MsgGetString(domain, msgid, STRING_ENCODING_UTF16_LE);
}
#endif
