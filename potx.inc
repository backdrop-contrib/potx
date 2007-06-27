<?php
// $Id$

/**
 * @file
 *   Extraction API used by the web and command line interface.
 *
 *   This include file implements the default string and file version
 *   storage as well as formatting of POT files for web download or
 *   file system level creation. The strings, versions and file contents
 *   are handled with global variables to reduce the memory overhead
 *   of passing them around. Custom string and version saving functions
 *   can be implemented to use the functionality provided here as an
 *   API for Drupal code to translatable string conversion.
 *
 *   The potx-cli.php script can be used with this include file as
 *   a command line interface to string extraction. The potx.module
 *   can be used as a web interface for manual extraction.
 *
 *   For a module using potx as an extraction API, but providing more
 *   sophisticated functionality on top of it, look into the
 *   'Localization server' module: http://drupal.org/project/l10n_server
 */

/**
 * Silence status reports.
 */
define('POTX_STATUS_SILENT', 0);

/**
 * Drupal message based status reports.
 */
define('POTX_STATUS_MESSAGE', 1);

/**
 * Command line status reporting:
 *  - status to standard output
 *  - errors to standard error
 */
define('POTX_STATUS_CLI', 2);

/**
 * Core parsing mode:
 *  - .info files folded into general.pot
 *  - separate files generated for modules
 */
define('POTX_BUILD_CORE', 0);

/**
 * Multiple files mode:
 *  - .info files folded into their module pot files
 *  - separate files generated for modules
 */
define('POTX_BUILD_MULTIPLE', 1);

/**
 * Single file mode:
 *  - all files folded into one pot file
 */
define('POTX_BUILD_SINGLE', 2);

/**
 * Save string to both installer and runtime collection.
 */
define('POTX_STRING_BOTH', 0);

/**
 * Save string to installer collection only.
 */
define('POTX_STRING_INSTALLER', 1);

/**
 * Save string to runtime collection only.
 */
define('POTX_STRING_RUNTIME', 2);

/**
 * Process a file and put extracted information to the given parameters.
 *
 * @param $file_path
 *   Comlete path to file to process.
 * @param $save_callback
 *   Callback function to use to save the collected strings.
 * @param $version_callback
 *   Callback function to use to save collected version numbers.
 * @param $strip_prefix
 *   An integer denoting the number of chars to strip from filepath for output.
 */
function _potx_process_file($file_path, $strip_prefix = 0, $save_callback = '_potx_save_string', $version_callback = '_potx_save_version') {
  global $_potx_tokens, $_potx_lookup;

  // Figure out the basename and extension to select extraction method.
  $basename = basename($file_path);
  list($filebase, $fileext) = explode(".", $basename, 2);

  // Always grab the CVS version number from the code
  $code = file_get_contents($file_path);
  $file_name = $strip_prefix > 0 ? substr($file_path, $strip_prefix) : $file_path;
  _potx_find_version_number($code, $file_name, $version_callback);

  // The .info files are not PHP code, no need to tokenize.
  if ($fileext == 'info') {
    _potx_find_info_file_strings($file_path, $file_name, $save_callback);
    return;
  }

  // Extract raw PHP language tokens.
  $raw_tokens = token_get_all($code);
  unset($code);

  // Remove whitespace and possible HTML (the later in templates for example),
  // count line numbers so we can include them in the output.
  $_potx_tokens = array();
  $_potx_lookup = array();
  $token_number = 0;
  $line_number = 1;
  foreach ($raw_tokens as $token) {
    if ((!is_array($token)) || (($token[0] != T_WHITESPACE) && ($token[0] != T_INLINE_HTML))) {
      if (is_array($token)) {
        $token[] = $line_number;
         // Fill array for finding token offsets quickly.
         if ($token[0] == T_STRING || ($token[0] == T_VARIABLE && $token[1] == '$t')) {
           if (!isset($_potx_lookup[$token[1]])) {
             $_potx_lookup[$token[1]] = array();
           }
           $_potx_lookup[$token[1]][] = $token_number;
         }
      }
      $_potx_tokens[] = $token;
      $token_number++;
    }
    // Collect line numbers.
    if (is_array($token)) {
      $line_number += count(split("\n", $token[1])) - 1;
    }
    else {
      $line_number += count(split("\n", $token)) - 1;
    }
  }
  unset($raw_tokens);

  // Regular t() calls with different usages.
  _potx_find_t_calls($file_name, $save_callback);
  _potx_find_t_calls($file_name, $save_callback, '_locale_import_message', POTX_STRING_BOTH);
  _potx_find_t_calls($file_name, $save_callback, '$t', POTX_STRING_BOTH);
  _potx_find_t_calls($file_name, $save_callback, 'st', POTX_STRING_INSTALLER);

  // Watchdog calls have their first argument translated in Drupal 5.x.
  // TODO: need to update this for Drupal 6.x
  _potx_find_t_calls($file_name, $save_callback, 'watchdog');

  // Plurals need unique parsing.
  _potx_find_format_plural_calls($file_name, $save_callback);

  if ($fileext == 'module') {
    _potx_find_perm_hook($file_name, $filebase, $save_callback);
    // TODO: this is required for Drupal 6.x.
    // _potx_find_menu_hook($file, $filebase, $save_callback);
    _potx_find_node_types_hook($file_name, $filebase, $save_callback);
    // Add the module name.
    $save_callback($filebase, $file_name);
  }
  
  if ($fileext == 'theme') {
    // Add the theme name.
    $save_callback($filebase, $file_name);
  }

  // Special handling of some Drupal core files.
  if ($basename == 'locale.inc') {
    _potx_find_language_names($file_name, $save_callback);
  }
  if ($basename == 'locale.module') {
    _potx_add_date_strings($file_name, $save_callback);
  }
  if ($basename == 'common.inc') {
    _potx_add_format_interval_strings($file_name, $save_callback);
  }
}

/**
 * Creates complete file strings with _potx_store()
 *
 * @param $string_mode
 *   Strings to generate files for: POTX_STRING_RUNTIME or POTX_STRING_INSTALLER.
 * @param $build_mode
 *   Storage mode used: single, multiple or core
 * @param $force_name
 *   Forces a given file name to get used, if single mode is on, without extension
 * @param $save_callback
 *   Callback used to save strings previously.
 * @param $version_callback
 *   Callback used to save versions previously.
 * @param $header_callback
 *   Callback to invoke to get the POT header.
 */
function _potx_build_files($string_mode = POTX_STRING_RUNTIME, $build_mode = POTX_BUILD_SINGLE, $force_name = 'general',  $save_callback = '_potx_save_string', $version_callback = '_potx_save_version', $header_callback = '_potx_get_header') {
  global $_potx_store;

  // Get strings and versions by reference.
  $strings  = $save_callback(NULL, NULL, 0, $string_mode);
  $versions = $version_callback();

  foreach ($strings as $string => $file_info) {
    // Build a compact list of files this string occured in.
    $occured = $file_list = array();
    foreach ($file_info as $file => $lines) {
      $occured[] = "$file:". join(';', $lines);
      if (isset($versions[$file])) {
        $file_list[] = $versions[$file];
      }
    }

    // Mark duplicate strings (both translated in the app and in the installer).
    $comment = join(" ", $occured);
    if (strpos($comment, '(dup)') !== FALSE) {
      $comment = '(duplicate) '. str_replace('(dup)', '', $comment);
    }
    $output = "#: $comment\n";

    // File name forcing in single mode.
    if ($build_mode == POTX_BUILD_SINGLE) {
      $file_name = $force_name;
    }
    // Store .info file strings either in general.pot or the module pot file,
    // depending on the mode used.
    elseif (strpos($comment, '.info')) {
      $file_name = ($build_mode == POTX_BUILD_CORE ? 'general' : str_replace('.info', '.module', $file_name));
    }
    // Else if occured more than once, store in general.pot.
    else {
      $file_name = (count($occured) > 1 ? 'general' : $file);
    }

    if (strpos($string, "\0") !== FALSE) {
      // Plural strings have a null byte delimited format.
      list($singular, $plural) = explode("\0", $string);
      $output .= "msgid \"$singular\"\n";
      $output .= "msgid_plural \"$plural\"\n";
      $output .= "msgstr[0] \"\"\n";
      $output .= "msgstr[1] \"\"\n";
    }
    else {
      // Simple strings.
      $output .= "msgid \"$string\"\n";
      $output .= "msgstr \"\"\n";
    }
    $output .= "\n";

    // Store the generated output in the given file storage.
    if (!isset($_potx_store[$file_name])) {
      $_potx_store[$file_name] = array(
        'header'  => $header_callback($file_name),
        'sources' => $file_list,
        'strings' => $output,
        'count'   => 1,
      );
    }
    else {
      // Maintain a list of unique file names.
      $_potx_store[$file_name]['sources']  = array_unique(array_merge($_potx_store[$file_name]['sources'], $file_list));
      $_potx_store[$file_name]['strings'] .= $output;
      $_potx_store[$file_name]['count']   += 1;
    }
  }
}

/**
 * Returns a header generated for a given file
 *
 * @param $file
 *   Name of POT file to generate header for
 */
function _potx_get_header($file) {
  $output  = '# $'.'Id'.'$'."\n";
  $output .= "#\n";
  $output .= '# LANGUAGE translation of Drupal ('. $file .")\n";
  $output .= "# Copyright YEAR NAME <EMAIL@ADDRESS>\n";
  $output .= "# --VERSIONS--\n";
  $output .= "#\n";
  $output .= "#, fuzzy\n";
  $output .= "msgid \"\"\n";
  $output .= "msgstr \"\"\n";
  $output .= "\"Project-Id-Version: PROJECT VERSION\\n\"\n";
  $output .= "\"POT-Creation-Date: " . date("Y-m-d H:iO") . "\\n\"\n";
  $output .= "\"PO-Revision-Date: YYYY-mm-DD HH:MM+ZZZZ\\n\"\n";
  $output .= "\"Last-Translator: NAME <EMAIL@ADDRESS>\\n\"\n";
  $output .= "\"Language-Team: LANGUAGE <EMAIL@ADDRESS>\\n\"\n";
  $output .= "\"MIME-Version: 1.0\\n\"\n";
  $output .= "\"Content-Type: text/plain; charset=utf-8\\n\"\n";
  $output .= "\"Content-Transfer-Encoding: 8bit\\n\"\n";
  $output .= "\"Plural-Forms: nplurals=INTEGER; plural=EXPRESSION;\\n\"\n\n";
  return $output;
}

/**
 * Write out generated files to the current folder.
 *
 * @param $http_filename
 *   File name for content-disposition header in case of usage
 *   over HTTP. If not given, files are written to the local filesystem.
 * @param $content_disposition
 *   See RFC2183. 'inline' or 'attachment', with a default of
 *   'inline'. Only used if $http_filename is set.
 * @todo
 *   Look into whether multiple files can be output via HTTP.
 */
function _potx_write_files($http_filename = NULL, $content_disposition = 'inline') {
  global $_potx_store;
  
  // Possibly merge some files if we have more then one.
  if (count($_potx_store) > 1) {
    foreach ($_potx_store as $file => $contents) {
      // Merge too small files into general.pot.
      if ($contents['count'] < 10 && $file != 'general') {
        if (!isset($_potx_store['general'])) {
          $_potx_store['general'] = $contents;
        }
        else {
          $_potx_store['general']['sources']  = array_unique(array_merge($_potx_store['general']['sources'], $contents['sources']));
          $_potx_store['general']['strings'] .= $contents['strings'];
        }
        // Drop this file, contents are integrated into general.pot.
        unset($_potx_store[$file]);
      }
    }
  }

  // Generate file lists and output files.
  foreach ($_potx_store as $file => $contents) {
    // Build replacement for file listing.
    if (count($contents['sources']) > 1) {
      $filelist = "Generated from files:\n#  " . join("\n#  ", $contents['sources']);
    }
    elseif (count($contents['sources']) == 1) {
      $filelist = "Generated from file: " . join('', $contents['sources']);
    }
    else {
      $filelist = 'No version information was available in the source files.';
    }
    $output = str_replace('--VERSIONS--', $filelist, $contents['header'] . $contents['strings']);

    if ($http_filename) {
      // HTTP output.
      header('Content-Type: text/plain; charset=utf-8');
      header('Content-Transfer-Encoding: 8bit');
      header("Content-Disposition: $content_disposition; filename=$http_filename");
      print $output;
      return;
    }
    else {
      // Local file output, flatten directory structure.
      $file = str_replace('.', '-', preg_replace('<[/]?([a-zA-Z]*/)*>', '', $file)) .'.pot';
      $fp = fopen($file, 'w');
      fwrite($fp, $output);
      fclose($fp);
    }
  }
}

/**
 * Escape quotes in a strings depending on the surrounding
 * quote type used.
 *
 * @param $str
 *   The strings to escape
 */
function _potx_format_quoted_string($str) {
  $quo = substr($str, 0, 1);
  $str = substr($str, 1, -1);
  if ($quo == '"') {
    $str = stripcslashes($str);
  }
  else {
    $str = strtr($str, array("\\'" => "'", "\\\\" => "\\"));
  }
  return addcslashes($str, "\0..\37\\\"");
}

/**
 * Output a marker error with an extract of where the error was found
 *
 * @param $file
 *   Name of file
 * @param $line
 *   Line number of error
 * @param $marker
 *   Function name with which the error was identified
 * @param $ti
 *   Index on the token array
 */
function _potx_marker_error($file, $line, $marker, $ti) {
  global $_potx_tokens;
  
  $tokens = '';
  $ti += 2;
  $tc = count($_potx_tokens);
  $par = 1;
  while ((($tc - $ti) > 0) && $par) {
    if (is_array($_potx_tokens[$ti])) {
      $tokens .= $_potx_tokens[$ti][1];
    }
    else {
      $tokens .= $_potx_tokens[$ti];
      if ($_potx_tokens[$ti] == "(") {
        $par++;
      }
      else if ($_potx_tokens[$ti] == ")") {
        $par--;
      }
    }
    $ti++;
  }
  _potx_status(t("Invalid marker content in %filename:%lineno\n* %marker(%tokens\n\n", array('%filename' => $file, '%lineno' => $line, '%marker' => $marker, '%tokens' => $tokens)), 'error');
}

/**
 * Status notification function.
 *
 * @param $text
 *   Text of status message, or POTX_STATUS_* constant
 *   to set status reporting mode.
 * @param $type
 *   Type of message: 'status' or 'error'.
 */
function _potx_status($text, $type = 'status') {
  static $reporting = POTX_STATUS_CLI;
  
  // Modify reporting type, if given.
  if (!is_string($text)) {
    $reporting = $text;
    return;
  }
  else {
    switch ($reporting) {
      case POTX_STATUS_MESSAGE:
        drupal_set_message($text, $type);
        break;
      case POTX_STATUS_CLI:
        fwrite($type == 'error' ? STDERR : STDOUT, $text);
        break;
      case POTX_STATUS_SILENT:
        // Do nothing.
        break;
    }
  }
}

/**
 * Detect all occurances of t()-like calls.
 *
 * These sequences are searched for:
 *   T_STRING("$function_name") + "(" + T_CONSTANT_ENCAPSED_STRING + ")"
 *   T_STRING("$function_name") + "(" + T_CONSTANT_ENCAPSED_STRING + ","
 *
 * @param $file
 *   Name of file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 * @param function_name
 *   The name of the function to look for (could be 't', '$t', 'st',
 *   'watchdog' or any other t-like function).
 * @param $string_mode
 *   String mode to use: POTX_STRING_INSTALLER, POTX_STRING_RUNTIME or
 *   POTX_STRING_BOTH.
 */
function _potx_find_t_calls($file, $save_callback, $function_name = 't', $string_mode = POTX_STRING_RUNTIME) {
  global $_potx_tokens, $_potx_lookup;
  
  // Lookup tokens by function name.
  if (is_array($_potx_lookup[$function_name])) {
    foreach ($_potx_lookup[$function_name] as $ti) {
      list($ctok, $par, $mid, $rig) = array($_potx_tokens[$ti], $_potx_tokens[$ti+1], $_potx_tokens[$ti+2], $_potx_tokens[$ti+3]);
      list($type, $string, $line) = $ctok;
      if ($par == "(") {
        if (in_array($rig, array(")", ","))
          && (is_array($mid) && ($mid[0] == T_CONSTANT_ENCAPSED_STRING))) {
            $save_callback(_potx_format_quoted_string($mid[1]), $file, $line, $string_mode);
        }
        else {
          // $function_name() found, but inside is something which is not a string literal.
          _potx_marker_error($file, $line, $function_name, $ti);
        }
      }
    }
  }
}

/**
 * Detect all occurances of format_plural calls.
 *
 * These sequences are searched for:
 *   T_STRING("format_plural") + "(" + ..anything (might be more tokens).. +
 *   "," + T_CONSTANT_ENCAPSED_STRING +
 *   "," + T_CONSTANT_ENCAPSED_STRING + ")"
 *
 * @param $file
 *   Name of file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_find_format_plural_calls($file, $save_callback) {
  global $_potx_tokens, $_potx_lookup;

  if (is_array($_potx_lookup['format_plural'])) {
    foreach ($_potx_lookup['format_plural'] as $ti) {
      list($ctok, $par1) = array($_potx_tokens[$ti], $_potx_tokens[$ti+1]);
      list($type, $string, $line) = $ctok;
      if ($par1 == "(") {
        // Eat up everything that is used as the first parameter
        $tn = $ti + 2;
        $depth = 0;
        while (!($_potx_tokens[$tn] == "," && $depth == 0)) {
          if ($_potx_tokens[$tn] == "(") {
            $depth++;
          }
          elseif ($_potx_tokens[$tn] == ")") {
            $depth--;
          }
          $tn++;
        }
        // Get further parameters
        list($comma1, $singular, $comma2, $plural, $par2) = array($_potx_tokens[$tn], $_potx_tokens[$tn+1], $_potx_tokens[$tn+2], $_potx_tokens[$tn+3], $_potx_tokens[$tn+4]);
        if (($comma2 == ",") && ($par2 == ")") &&
          (is_array($singular) && ($singular[0] == T_CONSTANT_ENCAPSED_STRING)) &&
          (is_array($plural) && ($plural[0] == T_CONSTANT_ENCAPSED_STRING))) {
            $save_callback(
              _potx_format_quoted_string($singular[1]) ."\0". _potx_format_quoted_string($plural[1]),
              $file,
              $line
            );
        }
        else {
          // format_plural() found, but the parameters are not correct.
          _potx_marker_error($file, $line, "format_plural", $ti);
        }
      }
    }
  }
}

/**
 * Detect permission names from the hook_perm() implementations.
 * Note that this will get confused with a similar pattern in a comment,
 * and with dynamic permissions, which need to be accounted for.
 *
 * @param $file
 *   Full path name of file parsed.
 * @param $filebase
 *   Filenaname of file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_find_perm_hook($file, $filebase, $save_callback) {
  global $_potx_tokens, $_potx_lookup;

  if (is_array($_potx_lookup[$filebase .'_perm'])) {
    // Special case for node module, because it uses dynamic permissions.
    // Include the static permissions by hand. That's about all we can do here.
    if ($filebase == 'node') {
      $line = $_potx_tokens[$_potx_lookup['node_perm'][0]][2];
      // List from node.module 1.763 (checked in on 2006/12/29 at 21:25:36 by drumm)
      $nodeperms = array('administer content types', 'administer nodes', 'access content', 'view revisions', 'revert revisions');
      foreach ($nodeperms as $item) {
        $save_callback($item, $file, $line);
      }
    }
    else {
      foreach ($_potx_lookup[$filebase .'_perm'] as $ti) {
        $tn = $ti;
        while (is_array($_potx_tokens[$tn]) || $_potx_tokens[$tn] != '}') {
          if (is_array($_potx_tokens[$tn]) && $_potx_tokens[$tn][0] == T_CONSTANT_ENCAPSED_STRING) {
            $save_callback(_potx_format_quoted_string($_potx_tokens[$tn][1]), $file, $_potx_tokens[$tn][2]);
          }
          $tn++;
        }
      }
    }
  }
}

/**
 * Helper function to look up the token closing the current function.
 *
 * @param $here
 *   The token at the function name
 */
function _potx_find_end_of_function($here) {
  global $_potx_tokens;

  // Seek to open brace.
  while (is_array($_potx_tokens[$here]) || $_potx_tokens[$here] != '{') {
    $here++;
  }
  $nesting = 1;
  while ($nesting > 0) {
    $here++;
    if (!is_array($_potx_tokens[$here])) {
      if ($_potx_tokens[$here] == '}') {
        $nesting--;
      }
      if ($_potx_tokens[$here] == '{') {
        $nesting++;
      }
    }
  }
  return $here;
}

/**
 * List of menu item titles. Required for Drupal 6.x.
 *
 * @param $file
 *   Full path name of file parsed.
 * @param $filebase
 *   Filenaname of file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
/*function _potx_find_menu_hook($file, $filebase, $save_callback) {
  global $_potx_tokens, $_potx_lookup;
  
  if (is_array($_potx_lookup[$filebase .'_menu'])) {
    foreach ($_potx_lookup[$filebase .'_menu'] as $ti) {
      $end = _potx_find_end_of_function($ti);
      $tn = $ti;
      while ($tn < $end) {
        if ($_potx_tokens[$tn][0] == T_CONSTANT_ENCAPSED_STRING) {
          // TODO: also look up descriptions, this only searches for titles
          if ($_potx_tokens[$tn][1] == "'title'" || $_potx_tokens[$tn][1] == '"title"') {
            if ($_potx_tokens[$tn+1][0] == T_DOUBLE_ARROW) {
              if ($_potx_tokens[$tn+2][0] == T_CONSTANT_ENCAPSED_STRING) {
                $save_callback(
                  _potx_format_quoted_string($_potx_tokens[$tn+2][1]),
                  $file,
                  $_potx_tokens[$tn+2][2]
                );
                $tn+=2; // Jump forward by 2.
              }
            }
          }
        }
        $tn++;
      }
    }
  }
}*/

/**
 * Detect oldschool node type names from hook_node_types().
 *
 * @param $file
 *   Full path name of file parsed.
 * @param $filebase
 *   Filenaname of file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_find_node_types_hook($file, $filebase, $save_callback) {
  global $_potx_tokens, $_potx_lookup;

  if (is_array($_potx_lookup[$filebase .'_node_types'])) {
    foreach ($_potx_lookup[$filebase .'_node_types'] as $ti) {
      $line = $_potx_tokens[$ti][2]; // We can actually target the line each token is on, but this is enough.
      $end = _potx_find_end_of_function($ti);
      $tn = $ti;
      while ($tn < $end) {
        if (is_array($_potx_tokens[$tn])) {
          if ($_potx_tokens[$tn][0] == T_CONSTANT_ENCAPSED_STRING) {
            $save_callback(_potx_format_quoted_string($_potx_tokens[$tn][1]), $file, $line);
          }
        }
        $tn++;
      }
    }
  }
}

/**
 * Get languages names from Drupal's locale.inc.
 *
 * @param $file
 *   Full path name of file parsed
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_find_language_names($file, $save_callback) {
  global $_potx_tokens, $_potx_lookup;

  foreach ($_potx_lookup['_locale_get_iso639_list'] as $ti) {
    // Search for the definition of _locale_get_iso639_list(), not where it is called.
    if ($_potx_tokens[$ti-1][0] == T_FUNCTION) {
      break;
    }
  }
    
  $end = _potx_find_end_of_function($ti);
  $ti += 7; // _locale_get_iso639_list, (, ), {, return, array, (
  while ($ti < $end) {
    while ($_potx_tokens[$ti][0] != T_ARRAY) {
      if (!is_array($_potx_tokens[$ti]) && $_potx_tokens[$ti] == ';') {
        // We passed the end of the list, break out to function level
        // to prevent an infinite loop.
        break 2;
      }
      $ti++;
    }
    $ti += 2; // array, (
    $save_callback(_potx_format_quoted_string($_potx_tokens[$ti][1]), $file, $_potx_tokens[$ti][2]);
  }
}

/**
 * Get the exact CVS version number from the file, so we can
 * push that into the generated output.
 *
 * @param $code
 *   Complete source code of the file parsed.
 * @param $file
 *   Name of the file parsed.
 * @param $version_callback
 *   Callback used to save the version information.
 */
function _potx_find_version_number($code, $file, $version_callback) {
  // Prevent CVS from replacing this pattern with actual info.
  if (preg_match('!\\$I' . 'd: ([^\\$]+) Exp \\$!', $code, $version_info)) {
    $version_callback($version_info[1], $file);
  }
}

/**
 * Add date strings, which cannot be extracted otherwise.
 * This is called for locale.module.
 *
 * @param $file
 *   Name of the file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_add_date_strings($file, $save_callback) {
  for ($i = 1; $i <= 12; $i++) {
    $stamp = mktime(0, 0, 0, $i, 1, 1971);
    $save_callback(date("F", $stamp), $file);
    $save_callback(date("M", $stamp), $file);
  }
  for ($i = 0; $i <= 7; $i++) {
    $stamp = $i * 86400;
    $save_callback(date("D", $stamp), $file);
    $save_callback(date("l", $stamp), $file);
  }
}

/**
 * Add format_interval special strings, which cannot be
 * extracted otherwise. This is called for common.inc
 *
 * @param $file
 *   Name of the file parsed.
 * @param $save_callback
 *   Callback function used to save strings.
 */
function _potx_add_format_interval_strings($file, $save_callback) {
  $components = array(
    '1 year' => '@count years',
    '1 week' => '@count weeks',
    '1 day'  => '@count days',
    '1 hour' => '@count hours',
    '1 min'  => '@count min',
    '1 sec'  => '@count sec');

  foreach($components as $singular => $plural) {
    $save_callback($singular ."\0". $plural, $file);
  }
}

/**
 * Parse an .info file and add relevant strings to the list.
 *
 * @param $file_path
 *   Complete file path to load contents with.
 * @param $file_name
 *   Stripped file name to use in outpout.
 * @param $strings
 *   Current strings array
 */
function _potx_find_info_file_strings($file_path, $file_name, $save_callback) {
  $info = array();

  // TODO: update this for Drupal 6.x, where parse_ini_file
  // is not applicable anymore
  if (file_exists($file_path)) {
    $info = parse_ini_file($file_path);
  }

  // We need everything but dependencies and version, see
  // _module_parse_info_file() on what is a possible .info
  // file setting.
  unset($info['dependencies']);
  unset($info['version']);

  foreach ($info as $key => $item) {
    $save_callback($item, $file_name);
  }
}

/**
 * Collect a list of file names relevant for extraction,
 * starting from the given path.
 *
 * @param $path
 *   Where to start searching for files recursively.
 *   Provide non-empty path values with a trailing slash.
 * @param $basename
 *   Allows the restriction of search to a specific basename
 *   (ie. to collect files for a specific module).
 * @todo
 *   Add folder exceptions for other version control systems.
 */
function _potx_explore_dir($path = '', $basename = '*') {
  $files = glob("$path$basename.{php,inc,module,engine,theme,install,info,profile}", GLOB_BRACE);
  $dirs = glob("$path*", GLOB_ONLYDIR);
  foreach ($dirs as $dir) {
    if (!preg_match("!(^|.+/)(CVS|.svn|.git)$!", $dir)) {
      $files = array_merge($files, _potx_explore_dir("$dir/", $basename));
    }
  }
  // Skip the file of the CLI interface, because it contains
  // test strings, we don't need on an automated run.
  foreach ($files as $id => $file_name) {
    if (preg_match('!potx-cli.php$!', $file_name)) {
      unset($files[$id]);
    }
  }
  return $files;
}

/**
 * Default $version_callback used by the potx system. Saves values
 * to a global array to reduce memory consumption problems when
 * passing around big chunks of values.
 *
 * @param $value
 *   The ersion number value of $file. If NULL, the collected
 *   values are returned.
 * @param $file
 *   Name of file where the version information was found.
 */
function _potx_save_version($value = NULL, $file = NULL) {
  global $_potx_versions;
  
  if (isset($value)) {
    $_potx_versions[$file] = $value;
  }
  else {
    return $_potx_versions;
  }
}

/**
 * Default $save_callback used by the potx system. Saves values
 * to global arrays to reduce memory consumption problems when
 * passing around big chunks of values.
 *
 * @param $value
 *   The string value. If NULL, the array of collected values
 *   are returned for the given $string_mode.
 * @param $file
 *   Name of file where the string was found.
 * @param $line
 *   Line number where the string was found.
 * @param $string_mode
 *   String mode: POTX_STRING_INSTALLER, POTX_STRING_RUNTIME
 *   or POTX_STRING_BOTH.
 */
function _potx_save_string($value = NULL, $file = NULL, $line = 0, $string_mode = POTX_STRING_RUNTIME) {
  global $_potx_strings, $_potx_install;
  
  if (isset($value)) {
    switch ($string_mode) {
      case POTX_STRING_BOTH:
        $_potx_install[$value][$file][] = $line .' (dup)';
        // Break intentionally missing.
      case POTX_STRING_RUNTIME:
        $_potx_strings[$value][$file][] = $line;
        break;
      case POTX_STRING_INSTALLER:
        $_potx_install[$value][$file][] = $line;
        break;
    }
  }
  else {
    return ($string_mode == POTX_STRING_RUNTIME ? $_potx_strings : $_potx_install);
  }
}

if (!function_exists('t')) {
  // If invoked outside of Drupal, t() will not exist, but
  // used to format the error message, so we provide a replacement.
  function t($string, $args = array()) {
    return strtr($string, $args);
  }
}
