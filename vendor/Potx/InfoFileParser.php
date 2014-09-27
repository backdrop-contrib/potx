<?php

/**
 * Class InfoFileParser
 */
class Potx_InfoFileParser extends Potx_AbstractFileParser {

  /**
   * PRocess an info file.
   *
   * @param $filepath
   * @return mixed
   */
  public function processFile($filepath) {
    $info = array();

    if (file_exists($filepath)) {
      if ($this->api_version > POTX_API_5) {
        $info = drupal_parse_info_file($filepath);
      }
      else {
        $info = parse_ini_file($filepath);
      }
    }

    // We need the name, description and package values. Others,
    // like core and PHP compatibility, timestamps or versions
    // are not to be translated.
    foreach (array('name', 'description', 'package') as $key) {
      if (isset($info[$key])) {
        // No context support for .info file strings.
        call_user_func($this->string_save_callback, addcslashes($info[$key], "\0..\37\\\""), POTX_CONTEXT_NONE, $filepath);
      }
    }

    // Add regions names from themes.
    if (isset($info['regions']) && is_array($info['regions'])) {
      foreach ($info['regions'] as $region => $region_name) {
        // No context support for .info file strings.
        call_user_func($this->string_save_callback, addcslashes($region_name, "\0..\37\\\""), POTX_CONTEXT_NONE, $filepath);
      }
    }
  }
}
