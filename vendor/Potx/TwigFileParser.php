<?php

/**
 * Class TwigFileParser
 */
class Potx_TwigFileParser extends Potx_AbstractFileParser {

  /**
   * @param $filepath
   * @return mixed
   */
  public function processFile($filepath) {
    $code = file_get_contents($filepath);

    $twig_lexer = new Twig_Lexer();
    $stream = $twig_lexer->tokenize($code, $filepath);

    while (!$stream->isEOF()) {
      $token = $stream->next();
      // Capture strings translated with the t or trans filter.
      if ($token->test(Twig_Token::VAR_START_TYPE)) {
        $token = $stream->next();
        if ($token->test(Twig_Token::NAME_TYPE)) {
          continue;
        }
        $string = $token->getValue();
        $line = $token->getLine();
        $has_t = FALSE;
        $chained = FALSE;
        $is_concat = FALSE;

        if ($stream->test(Twig_Token::PUNCTUATION_TYPE, '.')) {
          $is_concat = TRUE;
        }

        while (!$stream->isEOF() && ($token = $stream->next()) && (!$token->test(Twig_Token::VAR_END_TYPE))) {
          if ($token->test(Twig_Token::PUNCTUATION_TYPE, '|')) {
            if ($stream->test(array('t', 'trans'))) {
              $has_t = TRUE;
            }
            else {
              $chained = TRUE;
            }
          }
        }

        if ($has_t) {
          if (!$chained && !$is_concat) {
            call_user_func($this->string_save_callback, _potx_format_quoted_string('"' . trim($string) . '"'), POTX_CONTEXT_NONE, $filepath, $line);
          }
          else {
            $message = t('Uses of the t filter in Twig templates should start with a single literal string, and should not be chained.');
            // TODO: Fill in specific URL for Twig documentation once it exists.
            potx_status('error', $message, $filepath, NULL, NULL, 'https://drupal.org/developing/api/localization');
          }
        }
      }
      elseif ($token->test(Twig_Token::BLOCK_START_TYPE)) {
        $token = $stream->next();
        if ($token->test('trans')) {
          $this->processTransTag($stream, $filepath);
        }
      }
    }
  }

  /**
   * Process a single translation tag
   *
   * @param \Twig_TokenStream $stream
   * @param $filepath
   */
  private function processTransTag(Twig_TokenStream $stream, $filepath) {
    $is_plural = FALSE;
    $context = POTX_CONTEXT_NONE;

    // If the current token in the stream is a string, this trans tag
    // has a simple string argument to be translated.
    $token = $stream->next();
    if ($token->test(Twig_Token::STRING_TYPE)) {
      $text = $token->getValue();
      // Check for context.
      $token = $stream->next();
      if ($token->test(Twig_Token::NAME_TYPE, 'with')) {
        $context = $this->findTransContext($stream);
      }
      call_user_func($this->string_save_callback, $this->formatQuotedString('"' . trim($text) . '"'), $context, $filepath, $token->getLine());
      return;
    }

    // Otherwise, we are in a trans/endtrans structure.
    $singular = '';
    $text = array();
    $line = $token->getLine();
    // Process the stream until we reach the endtrans tag.
    while (!$stream->isEOF() && (!$token->test('endtrans'))) {
      // If it's text, add it to the translation.
      if ($token->test(Twig_Token::TEXT_TYPE)) {
        $text[] = $token->getValue();
      }
      elseif ($token->test(Twig_Token::NAME_TYPE, 'with')) {
        $context = $this->findTransContext($stream);
      }
      elseif ($token->test('plural')) {
        $singular = implode('', $text);
        $is_plural = TRUE;
        $text = array();
        // Skip past the 'count' token.
        $stream->next();
      }
      elseif ($token->test(Twig_Token::VAR_START_TYPE)) {
        $name = array();
        while ($stream->look(1)->test(Twig_Token::PUNCTUATION_TYPE, '.')) {
          $token = $stream->next();
          $name[] = $token->getValue();
          $stream->next();
        }
        $token = $stream->next();
        $name[] = $token->getValue();

        $name = implode('.', $name);
        // Figure out if it's escaped, passthrough, or placeholder.
        $token = $stream->next();
        // If the next thing we see is }}, this is escaped.
        if ($token->test(Twig_Token::VAR_END_TYPE)) {
          $text[] = "@$name";
        }
        // If the next thing we see is |, this is either passthrough or placeholder.
        elseif ($token->test(Twig_Token::PUNCTUATION_TYPE, '|')) {
          $token = $stream->next();
          if ($token->getValue() == 'passthrough') {
            $text[] = "!$name";
          }
          elseif ($token->getValue() == 'placeholder') {
            $text[] = "%$name";
          }
        }
      }
      $token = $stream->next();
    }

    if ($is_plural) {
      $plural = implode('', $text);
      $string = $this->formatQuotedString('"' . trim($singular) . '"') . "\0" . $this->formatQuotedString('"' . trim($plural) . '"');
    }
    else {
      $string = $this->formatQuotedString('"' . trim(implode('', $text)) . '"');
    }
    call_user_func($this->string_save_callback, $string, $context, $filepath, $line);
  }

  /**
   * Look for a 'context' parameter in {% trans %} tags, that appear after the
   * 'with' keyword.
   *
   * @param \Twig_TokenStream $stream
   * @return null|string
   * @throws \Twig_Error_Syntax
   */
  private function findTransContext(Twig_TokenStream $stream) {
    $token = $stream->next();
    if ($token->test(Twig_Token::PUNCTUATION_TYPE, '{')) {
      while (!$stream->isEOF() && ($token = $stream->next()) && !$token->test(Twig_Token::PUNCTUATION_TYPE, '}')) {
        if ($token->test(Twig_Token::STRING_TYPE, 'context')) {
          // Skip the ':' character.
          $stream->next();

          $token = $stream->next();
          if ($token->test(Twig_Token::STRING_TYPE)) {
            return $token->getValue();
          }
        }
      }
    }

    return POTX_CONTEXT_NONE;
  }
}
