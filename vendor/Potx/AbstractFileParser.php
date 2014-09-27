<?php

/**
 * Class AbstractParser
 */
abstract class Potx_AbstractFileParser {

  // Callbacks for processing strings and files.
  protected $string_save_callback;
  protected $file_save_callback;

  // The api version for which to parse files.
  protected $api_version = POTX_API_CURRENT;

  /**
   * @param $filepath
   * @return mixed
   */
  abstract public function processFile($filepath);

  /**
   * String callback setter.
   *
   * @param mixed $string_save_callback
   */
  public function setStringSaveCallback($string_save_callback) {
    $this->string_save_callback = $string_save_callback;
    return $this;
  }

  /**
   * @param int $api_version
   */
  public function setApiVersion($api_version) {
    $this->api_version = $api_version;
    return $this;
  }

  /**
   * @param string $file_save_callback
   */
  public function setFileSaveCallback($file_save_callback) {
    // @TODO Remove?
    $this->file_save_callback = $file_save_callback;
    return $this;
  }

  /**
   * Escape quotes in a string depending on the surrounding quote type used.
   *
   * @param $str string
   *   The string to escape
   * @return string
   */
  protected function formatQuotedString($str) {
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
}
