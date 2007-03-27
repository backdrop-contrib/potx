<?php
// $Id$

/**
 * @file Common functions used by the web and command line interface.
 */

/**
 * Command line status reporting:
 *  - status to standard output
 *  - errors to standard error
 */
define('POTX_STATUS_CLI',    0);

/**
 * Silence status reports.
 */
define('POTX_STATUS_SILENT', 1);

/**
 * Process a file and put extracted information to the given parameters.
 *
 * @param $file
 *   Name of file to process
 * @param $strings
 *   Array to add the extracted strings into
 * @param $file_versions
 *   Array to add the identified CVS file versions into
 * @param $installer_strings
 *   Array to add any installer time strings identified
 */
function _potx_process_file($file, &$strings, &$file_versions, &$installer_strings) {
  $code = file_get_contents($file);

  _potx_find_version_number($code, $file, $file_versions);

  // The .info files are not PHP code, they need special care.
  if (strpos($file, '.info') !== FALSE) {
    _potx_find_info_file_strings($file, $strings);
    return;
  }

  // Extract raw PHP language tokens.
  $raw_tokens = token_get_all($code);

  // Remove whitespace and possible HTML (the later in templates for example),
  // count line numbers so we can include them in the output.
  $tokens = array();
  $lineno = 1;
  foreach ($raw_tokens as $tok) {
    if ((!is_array($tok)) || (($tok[0] != T_WHITESPACE) && ($tok[0] != T_INLINE_HTML))) {
      if (is_array($tok)) {
        $tok[] = $lineno;
      }
      $tokens[] = $tok;
    }
    if (is_array($tok)) {
      $lineno += count(split("\n", $tok[1])) - 1;
    } else {
      $lineno += count(split("\n", $tok)) - 1;
    }
  }
  unset($raw_tokens);

  // Regular t() calls with different usages
  _potx_find_t_calls($tokens, $file, $strings);
  _potx_find_t_calls($tokens, $file, $strings, '_locale_import_message');
  _potx_find_dollart_calls($tokens, $file, $strings);

  // Installer related strings.
  _potx_find_t_calls($tokens, $file, $installer_strings, 'st');
  _potx_find_t_calls($tokens, $file, $installer_strings, '_locale_import_message', TRUE);
  _potx_find_dollart_calls($tokens, $file, $installer_strings, TRUE);

  // Watchdog type names
  _potx_find_watchdog_calls($tokens, $file, $strings);

  // Plurals need unique formatting
  _potx_find_format_plural_calls($tokens, $file, $strings);

  // Identify special parts by searching for Drupal hooks
  _potx_find_perm_hook($code, $file, $strings);
  _potx_find_node_types_hook($code, $file, $strings);
  _potx_find_module_name($code, $file, $strings);

  // Locale.inc contains a language list we need
  _potx_find_language_names($code, $file, $strings);

  // Add some strings we cannot extract
  _potx_add_date_strings($file, $strings);
  _potx_add_format_interval_strings($file, $strings);
}

/**
 * Creates complete file strings with _potx_store()
 *
 * @param $strings
 *   The strings gathered
 * @param $file_versions
 *   Original source file versions to output into the templates
 * @param $forcename
 *   Forces a given file name to get used (specify without extension)
 */
function _potx_build_files(&$strings, &$file_versions, $forcename = NULL) {

  foreach ($strings as $str => $fileinfo) {
    // Build a compact list of files this strings occured in.
    $occured = $filelist = array();
    foreach ($fileinfo as $file => $lines) {
      $occured[] = "$file:" . join(";", $lines);
      if (isset($file_versions[$file])) {
        $filelist[] = $file_versions[$file];
      }
    }

    // Mark duplicate strings (both translated in the app and in the installer).
    $occurances = join(" ", $occured);
    if (strpos($occurances, '(dup)') !== FALSE) {
      $occurances = '(duplicate) ' . str_replace('(dup)', '', $occurances);
    }
    $output = "#: $occurances\n";

    // File name forcing if required.
    if (isset($forcename)) {
      $filename = $forcename;
    }
    // Store .info file strings either in general.pot or the module pot file,
    // depending on the infofolding mode used.
    elseif (strpos($occurances, '.info')) {
      $filename = ($GLOBALS['infofolding'] ? 'general' : str_replace('.info', '.module', $filename));
    }
    // Else if occured more than once, store in general.pot.
    else {
      $filename = (count($occured) > 1 ? 'general' : $file);
    }

    if (strpos($str, "\0") === FALSE) {
      $output .= "msgid \"$str\"\n";
      $output .= "msgstr \"\"\n";
    }
    else {
      // Plural strings have a null byte delimited format.
      list ($singular, $plural) = explode("\0", $str);
      $output .= "msgid \"$singular\"\n";
      $output .= "msgid_plural \"$plural\"\n";
      $output .= "msgstr[0] \"\"\n";
      $output .= "msgstr[1] \"\"\n";
    }
    $output .= "\n";

    // Store the generated output in the given file.
    _potx_store($filename, $output, $filelist);
  }
}

/**
 * Write out generated files to the current folder.
 *
 * @param $http_filename
 *   File name for content-disposition header in case of usage
 *   over HTTP. If not given, files are written local to the filesystem.
 * @param $content_disposition
 *   See RFC2183. 'inline' or 'attachment', with a default of
 *   'inline'. Only used if $http_filename is set.
 * @todo
 *   Look into whether multiple files can be output via HTTP
 */
function _potx_write_files($http_filename = NULL, $content_disposition = 'inline') {
  $output = _potx_store();

  // Merge too small files into general.pot.
  // Merge all files if generating output for HTTP response.
  foreach ($output as $file => $content) {
    if (isset($http_filename) || (count($content) <= 11 && $file != 'general')) {
      @$output['general'][1] = array_unique(array_merge($output['general'][1], $content[1]));
      if (!isset($output['general'][0])) {
        $output['general'][0] = $content[0];
      }
      unset($content[0]);
      unset($content[1]);
      foreach ($content as $msgid) {
        $output['general'][] = $msgid;
      }
      unset($output[$file]);
    }
  }

  // Generate file lists and output files.
  foreach ($output as $file => $content) {
    $file = str_replace('.', '-', preg_replace('<[/]?([a-zA-Z]*/)*>', '', $file)) .'.pot';
    $filelist = $content[1];
    unset($content[1]);
    if (count($filelist) > 1) {
      $filelist = "Generated from files:\n#  " . join("\n#  ", $filelist);
    }
    elseif (count($filelist) == 1) {
      $filelist = "Generated from file: " . join("", $filelist);
    }
    else {
      $filelist = "No version information was available in the source files.";
    }
    $content = str_replace("--VERSIONS--", $filelist, join("", $content));

    if ($http_filename) {
      header('Content-Type: text/plain; charset=utf-8');
      header('Content-Transfer-Encoding: 8bit');
      header("Content-Disposition: $content_disposition; filename=$http_filename");
      print $content;
      return;
    }

    $fp = fopen($file, 'w');
    fwrite($fp, $content);
    fclose($fp);
  }
}

/**
 * Store a POT file chunk into a given file.
 *
 * @param $file
 *   File name to store $input in, or NULL if the stored array should be returned.
 * @param $input
 *   File chunk to store in $file.
 * @param $filelist
 *   A list of files from which the current chunk was generated
 */
function _potx_store($file = NULL, $input = NULL, $filelist = array()) {
  static $storage = array();
  if (isset($file)) {
    if (isset($storage[$file])) {
     // Maintain a list of unique file names.
     $storage[$file][1] = array_unique(array_merge($storage[$file][1], $filelist));
     $storage[$file][] = $input;
    }
    else {
      $storage[$file] = array();
      // You can override the header (with a language team specific variant for example),
      // if you generate missing templates for yourself, and would like to work quickly.
      $storage[$file][0] = (function_exists('_potx_get_header_custom') ? _potx_get_header_custom($file) : _potx_get_header($file));
      $storage[$file][1] = $filelist;
      $storage[$file][2] = $input;
    }
  }
  else {
    return $storage;
  }
}

/**
 * Returns a header generated for a given file
 *
 * @param $file
 *   Name of POT file to generate header for
 */
function _potx_get_header($file) {
  $output  = '# $' . 'Id' . '$' . "\n";
  $output .= "#\n";
  $output .= "# LANGUAGE translation of Drupal (". $file .")\n";
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
  } else {
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
 * @param $tokens
 *   The current state of the tokens array
 * @param $ti
 *   Index on the token array
 */
function _potx_marker_error($file, $line, $marker, &$tokens, $ti) {
  _potx_status("Invalid marker content in $file:$line\n* $marker(", TRUE);
  $ti += 2;
  $tc = count($tokens);
  $par = 1;
  while ((($tc - $ti) > 0) && $par) {
    if (is_array($tokens[$ti])) {
      _potx_status($tokens[$ti][1], TRUE);
    } else {
      _potx_status($tokens[$ti], TRUE);
      if ($tokens[$ti] == "(") {
        $par++;
      }
      if ($tokens[$ti] == ")") {
        $par--;
      }
    }
    $ti++;
  }
  _potx_status("\n\n", TRUE);
}

/**
 * Status notification function
 *
 * @param $text
 *   Text of status message, or POTX_STATUS_* constant
 *   to set status reporting mode.
 * @param $error
 *   TRUE if the message is an error
 */
function _potx_status($text, $error = FALSE) {
  static $reporting = POTX_STATUS_CLI;
  
  // Modify status, if given.
  if (!is_string($text)) {
    $reporting = $text;
  }
  
  if ($reporting == POTX_STATUS_CLI) {
    if ($error) {
      fwrite(STDERR, $text);
    }
    else {
      print $text;
    }
  }
}

/**
 * Detect all occurances of t()-like calls.
 *
 * These sequences are searched for:
 *   T_STRING("$functionname") + "(" + T_CONSTANT_ENCAPSED_STRING + ")"
 *   T_STRING("$functionname") + "(" + T_CONSTANT_ENCAPSED_STRING + ","
 *
 * @param $tokens
 *   The current tokens array
 * @param $file
 *   Name of file parsed
 * @param $strings
 *   Current strings array
 * @param functionname
 *   The name of the function to look for (could be t, st or any other t-like function)
 * @param $markdup
 *   Mark strings as duplicate (ie possibly having translations elsewhere already).
 *   Used for installer strings, which get to a different file, so duplicate checking
 *   is not evident with standard gettext tools.
 */
function _potx_find_t_calls(&$tokens, $file, &$strings, $functionname = 't', $markdup = FALSE) {

  $ti = 0;
  $tc = count($tokens);

  while (($tc - $ti) > 3) {

    list($ctok, $par, $mid, $rig) = array($tokens[$ti], $tokens[$ti+1], $tokens[$ti+2], $tokens[$ti+3]);
    if (!is_array($ctok)) {
      $ti++;
      continue;
    }
    list($type, $string, $line) = $ctok;

    if (($type == T_STRING) && ($string == $functionname) && ($par == "(")) {

      if (in_array($rig, array(")", ","))
          && (is_array($mid) && ($mid[0] == T_CONSTANT_ENCAPSED_STRING))) {

        $strings[_potx_format_quoted_string($mid[1])][$file][] = $line . ($markdup ? '(dup)' : '');
      }

      // $functionname() found, but inside is something which is not a string literal
      else {
        _potx_marker_error($file, $line, $functionname, $tokens, $ti);
      }
    }
    $ti++;
  }
}

/**
 * Detect all occurances of $t() calles.
 *
 * These sequences are searched for:
 *   T_VARIABLE("$t") + "(" + T_CONSTANT_ENCAPSED_STRING + ")"
 *   T_VARIABLE("$t") + "(" + T_CONSTANT_ENCAPSED_STRING + ","
 *
 * @param $tokens
 *   The current tokens array
 * @param $file
 *   Name of file parsed
 * @param $strings
 *   Current strings array
 * @param $markdup
 *   Mark strings as duplicate (ie possibly having translations elsewhere already).
 *   Used for installer strings, which get to a different file, so duplicate checking
 *   is not evident with standard gettext tools.
 */
function _potx_find_dollart_calls(&$tokens, $file, &$strings, $markdup = FALSE) {

  $ti = 0;
  $tc = count($tokens);

  while (($tc - $ti) > 3) {

    list($ctok, $par, $mid, $rig) = array($tokens[$ti], $tokens[$ti+1], $tokens[$ti+2], $tokens[$ti+3]);
    if (!is_array($ctok)) {
      $ti++;
      continue;
    }
    list($type, $string, $line) = $ctok;

    if (($type == T_VARIABLE) && ($string == '$t') && ($par == "(")) {

      if (in_array($rig, array(")", ","))
          && (is_array($mid) && ($mid[0] == T_CONSTANT_ENCAPSED_STRING))) {

        $strings[_potx_format_quoted_string($mid[1])][$file][] = $line . ($markdup ? '(dup)' : '');
      }

      // $t() found, but inside is something which is not a string literal
      else {
        _potx_marker_error($file, $line, $functionname, $tokens, $ti);
      }
    }
    $ti++;
  }
}

/**
 * Detect all occurances this format_plural calls.
 *
 * These sequences are searched for:
 *   T_STRING("format_plural") + "(" + ..anything (might be more tokens).. +
 *   "," + T_CONSTANT_ENCAPSED_STRING +
 *   "," + T_CONSTANT_ENCAPSED_STRING + ")"
 *
 * @param $tokens
 *   The current tokens array
 * @param $file
 *   Name of file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_format_plural_calls(&$tokens, $file, &$strings) {

  $ti = 0;
  $tc = count($tokens);

  while (($tc - $ti) > 7) {

    list($ctok, $par1) = array($tokens[$ti], $tokens[$ti+1]);
    if (!is_array($ctok)) {
      $ti++;
      continue;
    }
    list($type, $string, $line) = $ctok;

    if (($type == T_STRING) && ($string == "format_plural") && ($par1 == "(")) {

      // Eat up everything that is used as the first parameter
      $tn = $ti+2;
      $depth = 0;
      while (!($tokens[$tn] == "," && $depth == 0)) {
        if ($tokens[$tn] == "(") {
          $depth++;
        }
        elseif ($tokens[$tn] == ")") {
          $depth--;
        }
        $tn++;
      }

      // Get further parameters
      list($comma1, $singular, $comma2, $plural, $par2) = array($tokens[$tn], $tokens[$tn+1], $tokens[$tn+2], $tokens[$tn+3], $tokens[$tn+4]);

      if (($comma2 == ",") && ($par2 == ")") &&
          (is_array($singular) && ($singular[0] == T_CONSTANT_ENCAPSED_STRING)) &&
          (is_array($plural) && ($plural[0] == T_CONSTANT_ENCAPSED_STRING))) {

        $strings[_potx_format_quoted_string($singular[1]) .
        "\0" .
        _potx_format_quoted_string($plural[1])][$file][] = $line;
      }

      // format_plural() found, but the parameters are not correct
      else {
        _potx_marker_error($file, $line, "format_plural", $tokens, $ti);
      }
    }
    $ti++;
  }
}

/**
 * Detect all occurances of watchdog calls and get the type names.
 *
 * This sequences is searched for:
 *   T_STRING("watchdog") + "(" + T_CONSTANT_ENCAPSED_STRING + ","
 *
 * @param $tokens
 *   The current tokens array
 * @param $file
 *   Name of file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_watchdog_calls(&$tokens, $file, &$strings) {

  $ti = 0;
  $tc = count($tokens);

  while (($tc - $ti) > 3) {

    list($ctok, $par, $mid, $rig) = array($tokens[$ti], $tokens[$ti+1], $tokens[$ti+2], $tokens[$ti+3]);
    if (!is_array($ctok)) {
      $ti++;
      continue;
    }
    list($type, $string, $line) = $ctok;

    if (($type == T_STRING) && ($string == "watchdog") && ($par == "(")) {

      if (($rig == ",")
          && (is_array($mid) && ($mid[0] == T_CONSTANT_ENCAPSED_STRING))) {

        $strings[_potx_format_quoted_string($mid[1])][$file][] = $line;
      }

      // watchdog() found, but inside is something which is not a string literal
      else {
        _potx_marker_error($file, $line, "watchdog", $tokens, $ti);
      }
    }
    $ti++;
  }
}

/**
 * Detect permission names from the hook_perm() implementations.
 * Note that this will get confused with a similar pattern in a comment,
 * and with dynamic permissions, which need to be accounted for.
 *
 * @param $code
 *   Complete source code of the file parsed
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_perm_hook($code, $file, &$strings) {

  // Only check for perm hooks with a static array of permission names, nothing else
  if (preg_match('!^(.+function \\w+_perm\\(\\) \\{\s+return)([^\\}]+)\\}!Us', $code, $hook_code)) {
    $lines = substr_count($hook_code[1], "\n") + 1;
    preg_match_all('!(["\'])([a-zA-Z -]+)\1!', $hook_code[2], $items, PREG_PATTERN_ORDER);
    foreach ($items[2] as $item) {
      $strings[$item][$file][] = $lines;
    }
  }

  // Special case for the dynamic node_perm hook, to include the static permissions at least
  if (preg_match('!^(.+function node_perm\\(\\) \\{)!Us', $code, $hook_code)) {
    $lines = substr_count($hook_code[1], "\n") + 1;
    // List from node.module 1.763 (checked in on 2006/12/29 at 21:25:36 by drumm)
    $nodeperms = array('administer content types', 'administer nodes', 'access content', 'view revisions', 'revert revisions');
    foreach ($nodeperms as $item) {
      $strings[$item][$file][] = $lines;
    }
  }
}

/**
 * Detect oldschool node type names from hook_node_types().
 * Note that this will get confused with a similar pattern in a comment.
 *
 * @param $code
 *   Complete source code of the file parsed
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_node_types_hook($code, $file, &$strings) {

  if (preg_match('!^(.+function \\w+_node_types\\(\\) \\{\s+return)([^\\}]+)\\}!Us', $code, $hook_code)) {
    $lines = substr_count($hook_code[1], "\n") + 1;
    preg_match_all('!(["\'])([0-9a-z-]+)\1!', $hook_code[2], $items, PREG_PATTERN_ORDER);
    foreach ($items[2] as $item) {
      $strings[$item][$file][] = $lines;
    }
  }
}

/**
 * Detect name of module from known hooks.
 * Note that this will get confused with a similar pattern in a comment.
 *
 * @todo
 *   Help hook is not the best nowadays (it is not implemented by many
 *   modules given the .info files), look for a different solution.
 *
 * @param $code
 *   Complete source code of the file parsed
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_module_name($code, $file, &$strings) {
  if (preg_match('!function (\\w+)_help\\(!', $code, $module_name) &&
      !in_array($module_name[1], array('menu_get_active', 'xmlrpc_server_method'))) {
    $strings[$module_name[1]][$file][] = 0;
  }
}

/**
 * Get languages names from locale.inc, if we encounter that file.
 *
 * @param $code
 *   Complete source code of the file parsed
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_language_names($code, $file, &$strings) {
  if (preg_match("!locale\\.inc$!", $file) &&
      preg_match("!^(.+function _locale_get_iso639_list\\(\\) {)([^\\}]+)\\}!Us", $code, $langcodes)) {
    $lines = substr_count($langcodes[1], "\n") + 1;
    preg_match_all('!array\\((["\'])([^\'"]+)\1!', $langcodes[2], $items, PREG_PATTERN_ORDER);
    foreach ($items[2] as $item) {
      $strings[$item][$file][] = $lines;
    }
  }
}

/**
 * Get the exact CVS version number from the file, so we can
 * push that into the pot.
 *
 * @param $code
 *   Complete source code of the file parsed
 * @param $file
 *   Name of the file parsed
 * @param $file_versions
 *   The array storing file version information
 */
function _potx_find_version_number($code, $file, &$file_versions) {
  // Prevent CVS from replacing this pattern with actual info
  if (preg_match('!\\$I' . 'd: ([^\\$]+) Exp \\$!', $code, $version_info)) {
    $file_versions[$file] = $version_info[1];
  }
}

/**
 * Add date strings if locale.module is parsed. This cannot be
 * extracted otherwise.
 *
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_add_date_strings($file, &$strings) {
  if (preg_match('!(^|/)locale.module$!', $file)) {
    for ($i = 1; $i <= 12; $i++) {
      $stamp = mktime(0, 0, 0, $i, 1, 1971);
      $strings[date("F", $stamp)][$file][] = 0;
      $strings[date("M", $stamp)][$file][] = 0;
    }

    for ($i = 0; $i <= 7; $i++) {
      $stamp = $i * 86400;
      $strings[date("D", $stamp)][$file][] = 0;
      $strings[date("l", $stamp)][$file][] = 0;
    }
  }
}

/**
 * Add format_interval special strings if common.inc is parsed.
 * This cannot be extracted otherwise.
 *
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_add_format_interval_strings($file, &$strings) {
  if (preg_match('!(^|/)common.inc$!', $file)) {
    $components = array(
      '1 year' => '@count years',
      '1 week' => '@count weeks',
      '1 day'  => '@count days',
      '1 hour' => '@count hours',
      '1 min'  => '@count min',
      '1 sec'  => '@count sec');

    foreach($components as $singular => $plural) {
      $strings[$singular."\0".$plural][$file][] = 0;
    }
  }
}

/**
 * Parse an .info file and add relevant strings to the list.
 *
 * @param $file
 *   Name of the file parsed
 * @param $strings
 *   Current strings array
 */
function _potx_find_info_file_strings($file, &$strings) {
  $info = array();

  if (file_exists($file)) {
    $info = parse_ini_file($file);
  }

  // We need everything but dependencies and version, see
  // _module_parse_info_file() on what is a possible .info
  // file setting.
  unset($info['dependencies']);
  unset($info['version']);

  foreach ($info as $key => $item) {
    $strings[$item][$file][] = 0;
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
function _potx_explore_dir($path = '', $basename) {
  $files = glob("$path$basename.{php,inc,module,engine,theme,install,info,profile}", GLOB_BRACE);
  $dirs = glob("$path*", GLOB_ONLYDIR);
  foreach ($dirs as $dir) {
    if (!preg_match("!(^|.+/)(CVS|.svn|.git)$!", $dir)) {
      $files = array_merge($files, _potx_explore_dir("$dir/", $basename));
    }
  }
  // Skip the file of the CLI interface, because it contains test strings,
  // we don't need on a normal run.
  if (($id = array_search('potx-cli.php', $files)) !== FALSE) {
    unset($files[$id]);
  }
  return $files;
}
