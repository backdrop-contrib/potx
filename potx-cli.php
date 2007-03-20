<?php
// $Id$

/**
 * @file Translation template generator for Drupal (command line version).
 *
 * Extracts translatable strings from t(), t(,array()), format_plural()
 * and other function calls, plus adds some file specific strings. Only
 * literal strings with no embedded variables can be extracted. Generates
 * POT files, errors are printed on STDERR (in command line mode).
 */

// Functions shared with web based interface
include 'potx.inc';

// We need a lot of resources probably
@ini_set('memory_limit', '16M');
@set_time_limit(0);

if (!defined("STDERR")) {
  define('STDERR', fopen('php://stderr', 'w'));
}

$infofolding = FALSE;
$argv = $GLOBALS['argv'];
array_shift ($argv);
if (count($argv)) {
  switch ($argv[0]) {
    case '--help' :
      print "Drupal translation template generator\n";
      print "Usage: extractor.php [OPTION]\n\n";
      print "Possible options:\n";
      print " --auto             Autodiscovers files in current folder (default)\n";
      print " --files            Specify a list of files to generate templates for\n";
      print " --infofold=general Fold .info strings into general.pot (for core)\n";
      print " --infofold=module  Fold .info strings into module template (for contrib, default)\n";
      print " --debug            Only perform a 'self test'\n";
      print " --help             Display this message\n\n";
      print "You can also drop this file to a folder, and access it from\n";
      print "your browser. It will generate template files for all Drupal\n";
      print "files in all subfolders recursively.\n";
      return 1;
      break;
    case '--files' :
      array_shift($argv);
      $files = $argv;
      break;
    case '--infofold=general' :
      $infofolding = TRUE;
      $files = _potx_explore_dir();
      break;
    case '--debug' :
      $files = array(__FILE__);
      break;
    case '--auto' :
      $files = _potx_explore_dir();
      break;
  }
}
else {
  $files = _potx_explore_dir();
}

$strings = $file_versions = $installer_strings = array();

foreach ($files as $file) {
  _potx_status("Processing $file...\n");
  _potx_process_file($file, $strings, $file_versions, $installer_strings);
}

_potx_build_files($strings, $file_versions);
_potx_build_files($installer_strings, $file_versions, 'installer');
_potx_write_files();
_potx_status("\nDone.\n");

return;

// These are never executed, you can run potx-cli.php on itself to test it
// -----------------------------------------------------------------------------

$a = t("Test string 1" );
$b = t("Test string 2 %string", array("%string" => "how do you do"));
$c = t('Test string 3');
$d = t("Special\ncharacters");
$e = t('Special\ncharacters');
$f = t("Embedded $variable");
$g = t('Embedded $variable');
$h = t("more \$special characters");
$i = t('even more \$special characters');
$j = t("Mixed 'quote' \"marks\"");
$k = t('Mixed "quote" \'marks\'');
$l = t('This is some repeating text');
$m = t("This is some repeating text");
$n = t(embedded_function_call());
$o = format_plural($days, "one day", "@count days");
$p = format_plural(embedded_function_call($count), "one day", "@count days");

function embedded_function_call($dummy) { return 12; }

function potxcli_perm() {
  return array("access extrator data", 'administer extractor data');
}

function potxcli_help($section = 'default') {
  watchdog('help', t('Help called'));
  return t('This is some help');
}

function potxcli_node_types() {
  return array("extractor-cooltype", "extractor-evencooler");
}
