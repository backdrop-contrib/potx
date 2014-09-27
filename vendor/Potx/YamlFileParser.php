<?php

use Symfony\Component\Yaml\Yaml;

/**
 * Class YamlFileParser
 */
class Potx_YamlFileParser extends Potx_AbstractFileParser {

  protected $translation_patterns = array();

  // We'll need it in inner function, so better retain it instead of passing it
  // as a paramater.
  private $filepath;

  /**
   * Constructor
   */
  public function __construct() {
    // Load the default translation patterns.
    $path = drupal_get_path('module', 'potx') . '/yaml_translation_patterns.yml';
    $this->loadTranslationPatterns($path);
  }

  /**
   * @param $filepath
   * @return mixed
   */
  public function processFile($filepath) {
    $this->filepath = $filepath;

    // @TODO prevent global here
    global $_potx_config_set;

    $code = file_get_contents($filepath);

    foreach ($this->translation_patterns as $pattern => $trans_list) {
      if (fnmatch($pattern, $filepath) || fnmatch('*/' . $pattern, $filepath)) {
        $yaml = Yaml::parse($code);
        $this->findTranslatables($yaml, $trans_list, $filepath);
      }
    }

    if (preg_match('~config/schema/[^/]+\.yml$~', $filepath)) {
      $schema = Yaml::parse($code);
      foreach ($schema as $key => $element) {
        _potx_process_config_schema($key, $element);
      }
    }
    elseif (preg_match('~config/install/[^/]+\.yml$~', $filepath)) {
      $_potx_config_set[] = $filepath;
    }
  }

  /**
   * Find a translatable string in a yaml code.
   *
   * @param $yaml
   * @param $trans_list
   */
  private function findTranslatables($yaml, $trans_list) {
    $extract_values = FALSE;
    if (in_array('%top-level-key', $trans_list['keys'], TRUE)) {
      $extract_values = TRUE;
      $trans_list['keys'] = array_diff($trans_list['keys'], array('%top-level-key'));
    }

    foreach ($yaml as $key => $value) {

      if (in_array($key, $trans_list['keys'], TRUE)) {
        if (isset($trans_list['contexts'][$key])) {
          $context_key = $trans_list['contexts'][$key];
          if (isset($yaml[$context_key])) {
            $context = $yaml[$context_key];
          }
          else {
            $context = POTX_CONTEXT_NONE;
          }
        }
        else {
          $context = POTX_CONTEXT_NONE;
        }

        if (is_array($value)) {
          foreach ($value as $item) {
            call_user_func($this->string_save_callback, addcslashes($item, "\0..\37\\\""), $context, $this->filepath);
          }
        }
        else {
          call_user_func($this->string_save_callback, addcslashes($value, "\0..\37\\\""), $context, $this->filepath);
        }
      }
      elseif (is_array($value)) {
        $this->findTranslatables($value, $trans_list);
      }
      elseif ($extract_values) {
        call_user_func($this->string_save_callback, addcslashes($value, "\0..\37\\\""), POTX_CONTEXT_NONE, $this->filepath);
      }
    }
  }

  /**
   * @return mixed
   */
  public function getTranslationPatterns() {
    return $this->translation_patterns;
  }

  /**
   * Load YAML translation patterns from a file
   *
   * @param $path
   */
  public function loadTranslationPatterns($path) {
    // If the specified path is a directory, append the default file name.
    if (file_exists($path) && is_dir($path)) {
      $path .= 'yaml_translation_patterns.yml';
    }
    if (!file_exists($path)) {
      return;
    }
    $content = Yaml::parse(file_get_contents($path));

    foreach ($content as $pattern => $list) {
      foreach ($list as $value) {
        if (is_array($value)) {
          foreach ($value as $key => $context) {
            $this->translation_patterns[$pattern]['keys'][] = $key;
            $this->translation_patterns[$pattern]['contexts'][$key] = $context['context'];
          }
        }
        else {
          $this->translation_patterns[$pattern]['keys'][] = $value;
        }
      }
    }
  }
}
