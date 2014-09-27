<?php

/**
 * Class JavascriptFileParser
 */
class Potx_JavascriptFileParser extends Potx_AbstractFileParser {

  /**
   * Process a Javascript file.
   *
   * @param $filepath
   * @return mixed
   */
  public function processFile($filepath) {
    $code = file_get_contents($filepath);

    // Match all calls to Drupal.t() in an array.
    // Note: \s also matches newlines with the 's' modifier.
    preg_match_all('~
    [^\w]Drupal\s*\.\s*t\s*                     # match "Drupal.t" with whitespace
    \(\s*                                       # match "(" argument list start
    (' . POTX_JS_STRING . ')\s*                 # capture string argument
    (?:,\s*' . POTX_JS_OBJECT . '\s*            # optionally capture str args
      (?:,\s*' . POTX_JS_OBJECT_CONTEXT . '\s*) # optionally capture context
    ?)?                                         # close optional args
    \)                                          # match ")" to finish
    ~sx', $code, $t_matches, PREG_SET_ORDER);

    // Add strings from Drupal.t().
    if (isset($t_matches) && count($t_matches)) {
      foreach ($t_matches as $match) {
        // Remove match from code to help us identify faulty Drupal.t() calls.
        $code = str_replace($match[0], '', $code);

        // Get context
        if (!empty($match[2])) {
          // Remove wrapping quotes
          $context = $this->processString($match[2]);
        }
        else {
          // Set context to null
          $context = POTX_CONTEXT_NONE;
        }

        call_user_func($this->string_save_callback, $this->processString($match[1]), $context, $filepath, 0);
      }
    }

    // Match all Drupal.formatPlural() calls in another array.
    preg_match_all('~
    [^\w]Drupal\s*\.\s*formatPlural\s*  # match "Drupal.formatPlural" with whitespace
    \(                                  # match "(" argument list start
    \s*.+?\s*,\s*                       # match count argument
    (' . POTX_JS_STRING . ')\s*,\s*     # match singular string argument
    (                                   # capture plural string argument
      (?:                               # non-capturing group to repeat string pieces
        (?:
          \'                            # match start of single-quoted string
          (?:\\\\\'|[^\'])*             # match any character except unescaped single-quote
          @count                        # match "@count"
          (?:\\\\\'|[^\'])*             # match any character except unescaped single-quote
          \'                            # match end of single-quoted string
          |
          "                             # match start of double-quoted string
          (?:\\\\"|[^"])*               # match any character except unescaped double-quote
          @count                        # match "@count"
          (?:\\\\"|[^"])*               # match any character except unescaped double-quote
          "                             # match end of double-quoted string
        )
        (?:\s*\+\s*)?                   # match "+" with possible whitespace, for str concat
      )+                                # match multiple because we supports concatenating strs
    )\s*                                # end capturing of plural string argument
    (?:,\s*' . POTX_JS_OBJECT . '\s*              # optionally capture string args
      (?:,\s*' . POTX_JS_OBJECT_CONTEXT . '\s*)?  # optionally capture context
    )?
    \)                                            # match ")" to finish
    ~sx', $code, $plural_matches, PREG_SET_ORDER);

    if (isset($plural_matches) && count($plural_matches)) {
      foreach ($plural_matches as $index => $match) {
        // Remove match from code to help us identify faulty
        // Drupal.formatPlural() calls later.
        $code = str_replace($match[0], '', $code);

        // Get context
        if (!empty($match[3])) {
          // Remove wrapping quotes
          $context = $this->processString($match[3]);
        }
        else {
          // Set context to null
          $context = POTX_CONTEXT_NONE;
        }

        call_user_func($this->string_save_callback,
          $this->processString($match[1]) . "\0" . $this->processString($match[2]),
          $context,
          $filepath,
          0
        );
      }
    }

    // Any remaining Drupal.t() or Drupal.formatPlural() calls are evil. This
    // regex is not terribly accurate (ie. code wrapped inside will confuse
    // the match), but we only need some unique part to identify the faulty calls.
    preg_match_all('~[^\w]Drupal\s*\.\s*(t|formatPlural)\s*\([^)]+\)~s', $code, $faulty_matches, PREG_SET_ORDER);
    if (isset($faulty_matches) && count($faulty_matches)) {
      foreach ($faulty_matches as $index => $match) {
        $message = ($match[1] == 't') ? t('Drupal.t() calls should have a single literal string as their first parameter.') : t('The singular and plural string parameters on Drupal.formatPlural() calls should be literal strings, plural containing a @count placeholder.');
        potx_status('error', $message, $filepath, NULL, $match[0], 'http://drupal.org/node/323109');
      }
    }
  }

  /**
   * Process a single Javascript string.
   *
   * @param $string
   * @return string
   */
  private function processString($string) {
    return _potx_format_quoted_string(implode('', preg_split('~(?<!\\\\)[\'"]\s*\+\s*[\'"]~s', $string)));
  }
}
