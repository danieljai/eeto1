<?php
/**
 * @file
 *   Settings for mongodb. Moved back to module file.
 */

/**
 * Display watchdogs entries in mongodb.
 * @TODO
 *   Use theme function.
 *   Use exposed filter like dblog.
 */
function mongodb_watchdog_overview() {
  $rows = array();
  $icons = array(
    WATCHDOG_DEBUG    => '',
    WATCHDOG_INFO     => '',
    WATCHDOG_NOTICE   => '',
    WATCHDOG_WARNING  => theme('image', array('path' => 'misc/watchdog-warning.png', 'alt' => t('warning'), 'title' => t('warning'))),
    WATCHDOG_ERROR    => theme('image', array('path' => 'misc/watchdog-error.png', 'alt' => t('error'), 'title' => t('error'))),
    WATCHDOG_CRITICAL => theme('image', array('path' => 'misc/watchdog-error.png', 'alt' => t('critical'), 'title' => t('critical'))),
    WATCHDOG_ALERT    => theme('image', array('path' => 'misc/watchdog-error.png', 'alt' => t('alert'), 'title' => t('alert'))),
    WATCHDOG_EMERGENCY    => theme('image', array('path' => 'misc/watchdog-error.png', 'alt' => t('emergency'), 'title' => t('emergency'))),
  );
  $classes = array(
    WATCHDOG_DEBUG    => 'mongodb_watchdog-debug',
    WATCHDOG_INFO     => 'mongodb_watchdog-info',
    WATCHDOG_NOTICE   => 'mongodb_watchdog-notice',
    WATCHDOG_WARNING  => 'mongodb_watchdog-warning',
    WATCHDOG_ERROR    => 'mongodb_watchdog-error',
    WATCHDOG_CRITICAL => 'mongodb_watchdog-critical',
    WATCHDOG_ALERT    => 'mongodb_watchdog-alert',
    WATCHDOG_EMERGENCY => 'mongodb_watchdog-emergency',
  );

  global $pager_page_array, $pager_total, $pager_total_items, $pager_limits;
  $per_page = 50;
  $page = isset($_GET['page']) ? $_GET['page'] : '';
  $pager_page_array = explode(',', $page);
  $on_page = $pager_page_array[0];

  $cursor = mongodb_collection(variable_get('mongodb_collectionname', 'watchdog'))
    ->find(mongodb_watchdog_build_filter_query())
    ->limit($per_page)
    ->skip($on_page * $per_page)
    ->sort(array('timestamp' => -1));

  $build['mongodb_watchdog_filter_form'] = drupal_get_form('mongodb_watchdog_filter_form');
  $build['mongodb_watchdog_clear_log_form'] = drupal_get_form('mongodb_watchdog_clear_log_form');

  $header = array(
    '', // Icon column.
    t('#'),
    array('data' => t('Type')),
    array('data' => t('Date')),
    t('Source'),
    t('Message'),
  );

  $rows = array();
  foreach ($cursor as $id => $value) {
    if ($value['type'] == 'php' && $value['message'] == '%type: %message in %function (line %line of %file).') {
      $collection = mongodb_collection(variable_get('mongodb_watchdog_collectionname', 'watchdog'));
      $collection = $collection->db->selectCollection('watchdog_event_' . $value['_id']);
      if ($result = $collection->find()->sort(array('$natural' => -1))->limit(1)->getNext()) {
        $value['file'] = basename($result['variables']['%file']);
        $value['line'] = $result['variables']['%line'];
        $value['message'] = '%type in %function';
        $value['variables'] = $result['variables'];
      }
    }
    $message = truncate_utf8(strip_tags(_mongodb_watchdog_format_message($value)), 56, TRUE, TRUE);
    $rows[$id] = array(
      $icons[$value['severity']],
      isset($value['count']) && $value['count'] > 1 ? $value['count'] : '',
      t($value['type']),
      empty($value['timestamp']) ? '' : format_date($value['timestamp'], 'short'),
      empty($value['file']) ? '' : truncate_utf8(basename($value['file']), 30) . (empty($value['line']) ? '' : ('+' . $value['line'])),
      l($message, "admin/reports/mongodb-event/$id"),
    );
  }

  $build['mongodb_watchdog_table'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => $rows,
    '#attributes' => array('id' => 'admin-mongodb_watchdog'),
  );

  // Add the pager.
  if ($on_page > 0 || count($rows) >= $per_page) {
    $pager_total_items[0] = mongodb_collection(variable_get('mongodb_collectionname', 'watchdog'))
      ->find(mongodb_watchdog_build_filter_query())
      ->count();
    $pager_total[0] = ceil($pager_total_items[0] / $per_page);
    $pager_page_array[0] = max(0, min((int) $pager_page_array[0], ((int)$pager_total[0]) - 1));
    $pager_limits[0] = $per_page;
    $build['pager'] = array(
      '#theme' => 'pager',
    );
  }

  return $build;
}

/**
 * Display watchdogs entry details in mongodb.
 */
function mongodb_watchdog_event($dblog) {
  $severity = watchdog_severity_levels();
  $rows = array(
    array(
      array('data' => t('Type'), 'header' => TRUE),
      t($dblog['type']),
    ),
    array(
      array('data' => t('Severity'), 'header' => TRUE),
      $severity[$dblog['severity']],
    ),
    array(
      array('data' => t('Function'), 'header' => TRUE),
      isset($dblog['function']) ? $dblog['function'] : '',
    ),
    array(
      array('data' => t('File'), 'header' => TRUE),
      isset($dblog['file']) ? $dblog['file'] : '',
    ),
    array(
      array('data' => t('Line'), 'header' => TRUE),
      isset($dblog['line']) ? $dblog['line'] : '',
    ),
    array(
      array('data' => t('Count'), 'header' => TRUE),
      isset($dblog['count']) ? $dblog['count'] : '',
    ),
  );
  $build['reports'] = array( 
    '#type' => 'markup',
    '#markup' => l(t('Return to log report'), 'admin/reports/mongodb'),
  );
  $build['mongodb_watchdog_event_table']['header'] = array(
    '#theme' => 'table',
    '#rows' => $rows,
    '#attributes' => array('class' => array('dblog-event')),
  );
  // @todo: the count is unreliable, so just get the actual number of entries.
//$total = min($dblog['count'], variable_get('mongodb_watchdog_items', 10000));
  $collection = mongodb_collection(variable_get('mongodb_watchdog_collectionname', 'watchdog'));
  $collection = $collection->db->selectCollection('watchdog_event_' . $dblog['_id']);
  $total = $collection->count();
  $limit = 20;
  $pagenumber = mongodb_watchdog_pager_init(0, $limit, $total);
  $result = $collection
    ->find()
    ->skip($pagenumber * $limit)
    ->limit($limit)
    ->sort(array('$natural' => -1));
  $severity = watchdog_severity_levels();
  $rows = array();
  $header = array(
    array('data' => t('Date'), 'header' => TRUE),
    array('data' => t('User'), 'header' => TRUE),
    array('data' => t('Location'), 'header' => TRUE),
    array('data' => t('Referrer'), 'header' => TRUE),
    array('data' => t('Hostname'), 'header' => TRUE),
    array('data' => t('Message'), 'header' => TRUE),
    array('data' => t('Operations'), 'header' => TRUE),
  );
  foreach ($result as $event) {
    if (isset($event['wd-user'])) {
      $account = $event['wd-user'];
      unset($event['wd-user']);
      $ip = $dblog['ip'];
      $request_uri = $dblog['request_uri'];
      $referer = $dblog['referer'];
      $link = $dblog['link'];
      $dblog['variables'] = $event;
    }
    else {
      $account = $event['user'];
      $ip = $event['ip'];
      $request_uri = $event['request_uri'];
      $referer = $event['referer'];
      $link = $event['link'];
      $dblog['variables'] = $event['variables'];
    }
    $rows[] = array(
      format_date($event['timestamp'], 'short'),
      l($account['name'], 'user/' . $account['uid']),
      $request_uri ? l(truncate_utf8(basename(($request_uri)), 20), $request_uri) : '',
      $referer ? l(truncate_utf8(basename(($referer)), 20), $referer) : '',
      check_plain($ip),
      _mongodb_watchdog_format_message($dblog),
      $link,
    );
  }
  $build['mongodb_watchdog_event_table']['messages'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => $rows,
  );
  if ($total > $limit) {
    $build['mongodb_watchdog_event_table']['pager'] = array(
      '#theme' => 'pager',
    );
  }
  return $build;
}

/**
 * Initialize the global pager variables for use in a mongodb_watchdog event list. 
 */
function mongodb_watchdog_pager_init($element, $limit, $total) {
  global $pager_page_array, $pager_total, $pager_total_items;

  // Initialize pager, see pager.inc.
  $page = isset($_GET['page']) ? $_GET['page'] : '';
  $pager_page_array = explode(',', $page);
  if (!isset($pager_page_array[$element])) {
    $pager_page_array[$element] = 0;
  }
  $pager_total_items[$element] = $total;
  $pager_total[$element] = ceil($pager_total_items[$element] / $limit);
  $pager_page_array[$element] = max(0, min((int)$pager_page_array[$element], ((int)$pager_total[$element]) - 1));
  return isset($pager_page_array[$element]) ? $pager_page_array[$element] : 0;
}

/**
 * Formats a log message for display.
 *
 * @param $dblog
 *   An object with at least the message and variables properties
 */
function _mongodb_watchdog_format_message($dblog) {
  // Legacy messages and user specified text
  if (!isset($dblog['variables'])) {
    return $dblog['message'];
  }
  // Message to translate with injected variables
  return t($dblog['message'], $dblog['variables']);
}

/*
 * List mongodb_watchdog administration filters that can be applied.
 */
function mongodb_watchdog_filters() {
  $filters = array();

  foreach (_mongodb_watchdog_get_message_types() as $type) {
    $types[$type] = $type;
  }

  if (!empty($types)) {
    $filters['type'] = array(
      'title' => t('Type'),
      'options' => $types,
    );
  }

  $filters['severity'] = array(
    'title' => t('Severity'),
    'options' => watchdog_severity_levels(),
  );

  return $filters;
}

/**
 * Build the filter form.
 */
function mongodb_watchdog_filter_form($form) {
  $filters = mongodb_watchdog_filters();

  $form['filters'] = array(
    '#type' => 'fieldset',
    '#title' => t('Filter log messages'),
    '#collapsible' => TRUE,
    '#collapsed' => empty($session),
  );

  foreach ($filters as $key => $filter) {
    $form['filters']['status'][$key] = array(
      '#title' => $filter['title'],
      '#type' => 'select',
      '#multiple' => TRUE,
      '#size' => 8,
      '#options' => $filter['options'],
    );
    if (!empty($_SESSION['mongodb_watchdog_overview_filter'][$key])) {
      $form['filters']['status'][$key]['#default_value'] = $_SESSION['mongodb_watchdog_overview_filter'][$key];
    }
  }

  $form['filters']['buttons']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Filter'),
  );
  if (!empty($_SESSION['mongodb_watchdog_overview_filter'])) {
    $form['filters']['buttons']['reset'] = array(
      '#type' => 'submit',
      '#value' => t('Reset')
    );
  }

  return $form;
}

/**
 * Validate result from mongodb_watchdog administration filter form.
 */
function mongodb_watchdog_filter_form_validate($form, &$form_state) {
  if ($form_state['values']['op'] == t('Filter') && empty($form_state['values']['type']) && empty($form_state['values']['severity'])) {
    form_set_error('type', t('You must select something to filter by.'));
  }
}

/**
 * Process result from mongodb_watchdog administration filter form.
 */
function mongodb_watchdog_filter_form_submit($form, &$form_state) {
  $op = $form_state['values']['op'];
  $filters = mongodb_watchdog_filters();
  switch ($op) {
    case t('Filter'):
      foreach ($filters as $name => $filter) {
        if (isset($form_state['values'][$name])) {
          $_SESSION['mongodb_watchdog_overview_filter'][$name] = $form_state['values'][$name];
        }
      }
      break;
    case t('Reset'):
      $_SESSION['mongodb_watchdog_overview_filter'] = array();
      break;
  }
  return 'admin/reports/mongodb';
}

/**
 * Gets all available filter types.
 */
function _mongodb_watchdog_get_message_types() {
  // As of version 1.0.1, the PHP driver doesn't expose the 'distinct' command.
  $collection = mongodb_collection(variable_get('mongodb_collectionname', 'watchdog'));
  $result = $collection->db->command(array('distinct' => $collection->getName(), 'key' => 'type'));
  return $result['values'];
}

/**
 * Return form for mongodb_watchdog clear button.
 *
 * @ingroup forms
 * @see dblog_clear_log_submit()
 */
function mongodb_watchdog_clear_log_form($form) {
  $form['mongodb_watchdog_clear'] = array(
    '#type' => 'fieldset',
    '#title' => t('Clear log messages'),
    '#description' => t('This will permanently remove the log messages from the database.'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['mongodb_watchdog_clear']['clear'] = array(
    '#type' => 'submit',
    '#value' => t('Clear log messages'),
    '#submit' => array('mongodb_watchdog_clear_log_submit'),
  );

  return $form;
}

/**
 * Submit callback: clear database with log messages.
 */
function mongodb_watchdog_clear_log_submit() {
  try {
    // Drop the watchdog collection.
    $collection = mongodb_collection(variable_get('mongodb_collectionname', 'watchdog'));
    $collection->db->dropCollection($collection->getName());

    // Recreate the indexes.
    module_load_include('install', 'mongodb_watchdog');
    mongodb_watchdog_ensure_indexes();

    // Drop the event collections.
    foreach ($collection->db->listCollections() as $table) {
      $parts = explode('.', $table);
      if (substr($parts[1], 0, 15) == 'watchdog_event_') {
        $collection->db->dropCollection($table);
      }
    }

    drupal_set_message(t('MongoDB log cleared.'));
  }
  catch (Exception $e) {
    drupal_set_message(t('An error occured while clearing the MongoDB log.'), 'error');
  }
}

/**
 * Build a MongoDB query based on the selected filters.
 */
function mongodb_watchdog_build_filter_query() {
  if (empty($_SESSION['mongodb_watchdog_overview_filter'])) {
    return array();
  }

  // Build query.
  $where = $args = array();
  $types = $_SESSION['mongodb_watchdog_overview_filter']['type'] ? $_SESSION['mongodb_watchdog_overview_filter']['type'] : NULL;
  $severities = $_SESSION['mongodb_watchdog_overview_filter']['severity'] ? $_SESSION['mongodb_watchdog_overview_filter']['severity'] : NULL;

  $find = array();
  if ($types) {
    $find['type'] = array('$in' => $types);
  }
  if ($severities) {
    // MongoDB is picky about types, ensure the severities are all integers.
    $find['severity'] = array('$in' => array_map('intval', $severities));
  }
  return $find;
}
