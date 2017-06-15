<?php

/**
 * @file
 * Contains FeedsSpreadsheetParser.
 */
use Akeneo\Component\SpreadsheetParser\SpreadsheetParser;

/**
 * Provides feeds parser class for Excel files.
 */
class FeedsSpreadsheetParser extends FeedsParser {

  private $file_path;

  /**
   * Init file path.
   *
   * @param FeedsFetcherResult $fetcher_result
   *   Feeds Fetcher result object.
   */
  private function initFilePath(FeedsFetcherResult $fetcher_result) {
    if (!isset($this->file_path)) {
      $this->file_path = drupal_realpath($fetcher_result->getFilePath());
    }
  }

  /**
   * {@inheritdoc}
   */
  public function parse(FeedsSource $source, FeedsFetcherResult $fetcher_result) {
    $this->initFilePath($fetcher_result);

    // Init state. Use two pointers: global and by sheet.
    $state = $source->state(FEEDS_PARSE);

    if (empty($state->pointer)) {
      $state->pointer = array(
        'sheet_pointer' => 0,
        'row_pointer' => -1,
      );
    }

    // Prepare to parsing.
    $items = array();
    $spreadsheet = SpreadsheetParser::open($this->file_path);
    $all_sheets = $spreadsheet->getWorksheets();

    // Setup parse type.
    switch ($this->config['sheets']['parse_type']) {
      case 'first':
        $index = key($all_sheets);
        $sheets = [$index => $all_sheets[$index]];
        break;

      case 'title':
        $index = array_search($this->config['sheets']['parse_value'], $all_sheets);
        $sheets = [$index => $all_sheets[$index]];
        break;

      case 'index':
        $index = $this->config['sheets']['parse_value'];
        $sheets = [$index => $all_sheets[$index]];
        break;

      default:
        $sheets = $all_sheets;
    }

    // Check for errors.
    if (empty($sheets)) {
      throw new Exception(t('No sheets.'));
    }

    if (empty($state->total) && !isset($state->sheets_max_row)) {
      $state->sheets_max_row = [];
      $state->total = 0;
      foreach ($sheets as $sheet_num => $sheet_title) {
        $rowIterator = $spreadsheet->createRowIterator($sheet_num);
        $rowIterator->rewind();
        while ($rowIterator->valid()) {
          $rowIterator->next();
        }

        // The $rowIterator->key() will give us the last valid row number.
        $state->sheets_max_row[$sheet_num] = $rowIterator->key();
        $state->total += $rowIterator->key();
      }
    }

    $batch_processed = 0;
    foreach ($sheets as $sheet_num => $sheet_title) {

      if ($state->pointer['sheet_pointer'] && $state->pointer['sheet_pointer'] > $sheet_num) {
        // Skip the already processed sheets.
        continue;
      }
      elseif ($state->pointer['sheet_pointer'] == $sheet_num) {
        // Currently processed sheet.
        if ($state->pointer['row_pointer'] == $state->sheets_max_row[$sheet_num]) {
          // We are already on the last row, so continue from the next.
          continue;
        }
      }
      else {
        // New sheet. Move the pointer.
        $state->pointer['sheet_pointer'] = $sheet_num;
        $state->pointer['row_pointer'] = -1;
      }

      $rowIterator = $spreadsheet->createRowIterator($sheet_num);
      $headers = NULL;
      $keys = NULL;
      $batch_chunk_done = NULL;

      foreach ($rowIterator as $row_index => $row) {
        if ($headers === NULL) {
          $headers = $row;
          continue;
        }

        // Skip the already processed rows.
        if ($row_index <= $state->pointer['row_pointer']) {
          continue;
        }

        $row_key = $sheet_num . '__' . $row_index;

        // Iterate cells.
        foreach ($row as $cell_num => $cellValue) {
          // Make the result array with the correct values - by cell title.
          $cell_key = !empty($headers[$cell_num]) ? $headers[$cell_num] : $cell_num;

          $value = $cellValue;

          if (!is_null($value)) {
            $items[$row_key][$cell_key] = $value;
          }
        }

        // Add sheet title to values.
        if (isset($items[$row_key]) && !empty($this->config['sheets']['map_title'])) {
          $items[$row_key][$this->config['sheets']['map_title']] = $sheet_title;
        }

        $batch_processed++;
        if ($batch_processed >= $this->config['chunk_size']) {
          // We have processed enough items for one batch.
          $state->pointer['row_pointer'] = $row_index;
          $batch_chunk_done = TRUE;
          break 2;
        }
      }
    }

    if ($batch_chunk_done) {
      $processed = 0;
      foreach ($state->sheets_max_row as $sheet_num => $max_row) {
        if ($sheet_num < $state->pointer['sheet_pointer']) {
          $processed += $max_row;
        }
        elseif ($sheet_num == $state->pointer['sheet_pointer']) {
          $processed += $state->pointer['row_pointer'];
        }
      }
      $state->progress($state->total, $processed);
    }
    else {
      // All loops finished.
      $state->progress($state->total, $state->total);
    }

    return new FeedsParserResult(array_values($items));
  }

  /**
   * {@inheritdoc}
   */
  public function clear(FeedsSource $source) {

  }

  /**
   * {@inheritdoc}
   */
  public function getMappingSources() {
    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function configDefaults() {
    return array(
      'chunk_size' => 10,
      'sheets' => array(
        'parse_type' => 'all',
        'parse_value' => '',
        'map_title' => '',
      ),
    );
  }

  /**
   * {@inheritdoc}
   */
  public function configForm(&$form_state) {
    $form = array();
    $form['chunk_size'] = array(
      '#type' => 'select',
      '#title' => t('Chunk size'),
      '#options' => drupal_map_assoc(array(1, 5, 10, 20, 50, 100, 200, 500, 1000)),
      '#description' => t("Select quantity of rows which will be parsed per ounce. <b>Notice: selected quantity will be multiplied on sheets' quantity.</b>"),
      '#default_value' => $this->config['chunk_size'],
    );
    $form['sheets'] = array(
      '#type' => 'fieldset',
      '#title' => t('Sheets settings'),
      '#tree' => TRUE,
    );
    $form['sheets']['parse_type'] = array(
      '#type' => 'select',
      '#title' => t('How to parse?'),
      '#default_value' => $this->config['sheets']['parse_type'],
      '#options' => array(
        'all' => t('All sheets'),
        'first' => t('First sheet'),
        'title' => t('By title'),
        'index' => t('By index'),
      ),
    );
    $form['sheets']['parse_value'] = array(
      '#type' => 'textfield',
      '#title' => t('Sheet'),
      '#description' => t('Enter sheet title or index depending on the selected option.'),
      '#default_value' => $this->config['sheets']['parse_value'],
      '#states' => array(
        'visible' => array(
          array(
            array('select[name="sheets[parse_type]"]' => array('value' => 'title')),
            'or',
            array('select[name="sheets[parse_type]"]' => array('value' => 'index')),
          ),
        ),
      ),
    );
    $form['sheets']['map_title'] = array(
      '#type' => 'textfield',
      '#title' => t('Source key of sheet title'),
      '#description' => t('If filled, sheet title will be available in mapping.'),
      '#default_value' => $this->config['sheets']['map_title'],
    );
    return $form;
  }

}
