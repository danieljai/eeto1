<?php


if (!function_exists('_drupal_session_open')) {

include_once dirname(__FILE__) . '/../mongodb.module';

/**
 * @file
 * User session handling functions.
 *
 * The user-level session storage handlers:
 * - _drupal_session_open()
 * - _drupal_session_close()
 * - _drupal_session_read()
 * - _drupal_session_write()
 * - _drupal_session_destroy()
 * - _drupal_session_garbage_collection()
 * are assigned by session_set_save_handler() in bootstrap.inc and are called
 * automatically by PHP. These functions should not be called directly. Session
 * data should instead be accessed via the $_SESSION superglobal.
 */

/**
 * Session handler assigned by session_set_save_handler().
 *
 * This function is used to handle any initialization, such as file paths or
 * database connections, that is needed before accessing session data. Drupal
 * does not need to initialize anything in this function.
 *
 * This function should not be called directly.
 *
 * @return
 *   This function will always return TRUE.
 */
function _drupal_session_open() {
  return TRUE;
}

/**
 * Session handler assigned by session_set_save_handler().
 *
 * This function is used to close the current session. Because Drupal stores
 * session data in the database immediately on write, this function does
 * not need to do anything.
 *
 * This function should not be called directly.
 *
 * @return
 *   This function will always return TRUE.
 */
function _drupal_session_close() {
  return TRUE;
}

/**
 * Session handler assigned by session_set_save_handler().
 *
 * This function will be called by PHP to retrieve the current user's
 * session data, which is stored in the database. It also loads the
 * current user's appropriate roles into the user object.
 *
 * This function should not be called directly. Session data should
 * instead be accessed via the $_SESSION superglobal.
 *
 * @param $sid
 *   Session ID.
 * @return
 *   Either an array of the session data, or an empty string, if no data
 *   was found or the user is anonymous.
 */
function _drupal_session_read($sid) {
  global $user, $is_https;

  // Write and Close handlers are called after destructing objects
  // since PHP 5.0.5.
  // Thus destructors can use sessions but session handler can't use objects.
  // So we are moving session closure before destructing objects.
  register_shutdown_function('session_write_close');

  // Handle the case of first time visitors and clients that don't store
  // cookies (eg. web crawlers).
  $insecure_session_name = substr(session_name(), 1);
  if (!isset($_COOKIE[session_name()]) && !isset($_COOKIE[$insecure_session_name])) {
    $user = drupal_anonymous_user();
    return '';
  }

  // Otherwise, if the session is still active, we have a record of the
  // client's session in the database. If it's HTTPS then we are either have
  // a HTTPS session or we are about to log in so we check the sessions table
  // for an anonymous session wth the non-HTTPS-only cookie.
  $collection = mongodb_collection(variable_get('mongodb_session', 'session'));
  if ($is_https) {
    $session = $collection->findOne(array('ssid' => $sid));
    if (!$session) {
      if (isset($_COOKIE[$insecure_session_name])) {
        $session = $collection->findOne(array('sid' => $_COOKIE[$insecure_session_name], 'uid' => 0));
      }
    }
  }
  else {
    $session = $collection->findOne(array('sid' => $sid));
  }

  // We found the client's session record and they are an authenticated user.
  if ($session && !empty($session['uid'])) {
    $collection = mongodb_collection('fields_current', 'user');
    $user = (object) $collection->findOne(array('_id' => $session['uid']));
    // Copy from SQL.
    if (!isset($user->uid)) {
      include_once dirname(__FILE__) . '/mongodb_session.module';
      $user = db_query("SELECT * FROM {users} u WHERE uid = :uid", array(':uid' => $session['uid']))->fetchObject();
      $user->roles = mongodb_session_user_insert(array(), $user);
    }
    // If mongodb_fields_storage wrote this user object, we need to update it.
    elseif (!isset($user->roles)) {
      include_once dirname(__FILE__) . '/mongodb_session.module';
      $user->roles = _mongodb_session_get_roles($user);
      $collection->update(array('_id' => (int) $user->uid), array('$set' => array('roles' => $user->roles)));
    }
    $user->session = $session['session'];
  }
  // We didn't find the client's record (session has expired), or they
  // are an anonymous user.
  else {
    $user = drupal_anonymous_user();
    $user->session = !empty($session['session']) ? $session['session'] : '';
  }

  return $user->session;
}

/**
 * Session handler assigned by session_set_save_handler().
 *
 * This function will be called by PHP to store the current user's
 * session, which Drupal saves to the database.
 *
 * This function should not be called directly. Session data should
 * instead be accessed via the $_SESSION superglobal.
 *
 * @param $sid
 *   Session ID.
 * @param $value
 *   Serialized array of the session data.
 * @return
 *   This function will always return TRUE.
 */
function _drupal_session_write($sid, $value) {
  global $user, $is_https;

  if (!drupal_save_session()) {
    // We don't have anything to do if we are not allowed to save the session.
    return;
  }

  $fields = array(
    'uid' => (int)$user->uid,
    'cache' => isset($user->cache) ? (int)$user->cache : 0,
    'hostname' => ip_address(),
    'session' => $value,
    'timestamp' => REQUEST_TIME,
  );
  $key = array('sid' => $sid);
  if ($is_https) {
    $key['ssid'] = $sid;
    $insecure_session_name = substr(session_name(), 1);
    // The "secure pages" setting allows a site to simultaneously use both
    // secure and insecure session cookies. If enabled, use the insecure session
    // identifier as the sid.
    if (variable_get('https', FALSE) && isset($_COOKIE[$insecure_session_name])) {
      $key['sid'] = $_COOKIE[$insecure_session_name];
    }
    else {
      unset($key['sid']);
    }
  }

  $collection = mongodb_collection(variable_get('mongodb_session', 'session'));
  $collection
    ->update($key, $key + $fields, array('upsert' => TRUE));

  // Last access time is updated no more frequently than once every 180 seconds.
  // This reduces contention in the users table.
  if ($user->uid && REQUEST_TIME - $user->access > variable_get('session_write_interval', 180)) {
    $fields = array(
      'access' => REQUEST_TIME
    );
    // Update SQL because user_load() falls back to that.
    db_update('users')
      ->fields($fields)
      ->condition('uid', $user->uid)
      ->execute();
    // Update MongoDB because we prefer that.
    mongodb_collection('fields_current', 'user')->update(array('_id' => (int) $user->uid), array('$set' => $fields));
    // Throw a bone to entitycache, too.
    if (variable_get('entitycache_enabled', TRUE)) {
      cache_clear_all($user->uid, 'cache_entity_user');
    }
  }

  return TRUE;
}

/**
 * Initialize the session handler, starting a session if needed.
 */
function drupal_session_initialize() {
  global $user;

  session_set_save_handler('_drupal_session_open', '_drupal_session_close', '_drupal_session_read', '_drupal_session_write', '_drupal_session_destroy', '_drupal_session_garbage_collection');

  if (isset($_COOKIE[session_name()])) {
    // If a session cookie exists, initialize the session. Otherwise the
    // session is only started on demand in drupal_session_commit(), making
    // anonymous users not use a session cookie unless something is stored in
    // $_SESSION. This allows HTTP proxies to cache anonymous pageviews.
    drupal_session_start();
    if (!empty($user->uid) || !empty($_SESSION)) {
      drupal_page_is_cacheable(FALSE);
    }
  }
  else {
    // Set a session identifier for this request. This is necessary because
    // we lazyly start sessions at the end of this request, and some
    // processes (like drupal_get_token()) needs to know the future
    // session ID in advance.
    $user = drupal_anonymous_user();
    session_id(md5(uniqid('', TRUE)));
  }
}

/**
 * Forcefully start a session, preserving already set session data.
 *
 * @ingroup php_wrappers
 */
function drupal_session_start() {
  if (!drupal_session_started()) {
    // Save current session data before starting it, as PHP will destroy it.
    $session_data = isset($_SESSION) ? $_SESSION : NULL;

    session_start();
    drupal_session_started(TRUE);

    // Restore session data.
    if (!empty($session_data)) {
      $_SESSION += $session_data;
    }
  }
}

/**
 * Commit the current session, if necessary.
 *
 * If an anonymous user already have an empty session, destroy it.
 */
function drupal_session_commit() {
  global $user;

  if (!drupal_save_session()) {
    // We don't have anything to do if we are not allowed to save the session.
    return;
  }

  if (empty($user->uid) && empty($_SESSION)) {
    // There is no session data to store, destroy the session if it was
    // previously started.
    if (drupal_session_started()) {
      session_destroy();
    }
  }
  else {
    // There is session data to store. Start the session if it is not already
    // started.
    if (!drupal_session_started()) {
      drupal_session_start();
    }
    // Write the session data.
    session_write_close();
  }
}

/**
 * Return whether a session has been started.
 */
function drupal_session_started($set = NULL) {
  static $session_started = FALSE;
  if (isset($set)) {
    $session_started = $set;
  }
  return $session_started && session_id();
}

/**
 * Called when an anonymous user becomes authenticated or vice-versa.
 *
 * @ingroup php_wrappers
 */
function drupal_session_regenerate() {
  global $user, $is_https;
  if ($is_https && variable_get('https', FALSE)) {
    $insecure_session_name = substr(session_name(), 1);
    $params = session_get_cookie_params();
    $session_id = md5(uniqid(mt_rand(), TRUE));
    setcookie($insecure_session_name, $session_id, REQUEST_TIME + $params['lifetime'], $params['path'], $params['domain'], FALSE, $params['httponly']);
    $_COOKIE[$insecure_session_name] = $session_id;
  }

  if (drupal_session_started()) {
    $old_session_id = session_id();
    session_regenerate_id();
  }
  else {
    // Start the session when it doesn't exist yet.
    // Preserve the logged in user, as it will be reset to anonymous
    // by _drupal_session_read.
    $account = $user;
    drupal_session_start();
    $user = $account;
  }

  if (isset($old_session_id)) {
    $field = $is_https ? 'ssid' : 'sid';
    mongodb_collection(variable_get('mongodb_session', 'session'))
      ->update(array('sid' => $old_session_id), array('$set' => array($field => session_id())));
  }
}

/**
 * Session handler assigned by session_set_save_handler().
 *
 * Cleanup a specific session.
 *
 * @param string $sid
 *   Session ID.
 */
function _drupal_session_destroy($sid) {
  global $user, $is_https;

  $field = $is_https ? 'ssid' : 'sid';
  mongodb_collection(variable_get('mongodb_session', 'session'))
    ->remove(array($field => $sid));

  // Reset $_SESSION and $user to prevent a new session from being started
  // in drupal_session_commit().
  $_SESSION = array();
  $user = drupal_anonymous_user();

  // Unset the session cookies.
  _drupal_session_delete_cookie(session_name());
  if ($is_https) {
    _drupal_session_delete_cookie(substr(session_name(), 1), TRUE);
  }
}

/**
 * Deletes the session cookie.
 *
 * @param $name
 *   Name of session cookie to delete.
 * @param $force_insecure
 *   Fornce cookie to be insecure.
 */
function _drupal_session_delete_cookie($name, $force_insecure = FALSE) {
  if (isset($_COOKIE[$name])) {
    $params = session_get_cookie_params();
    setcookie($name, '', REQUEST_TIME - 3600, $params['path'], $params['domain'], !$force_insecure && $params['secure'], $params['httponly']);
    unset($_COOKIE[$name]);
  }
}

/**
 * End a specific user's session(s).
 *
 * @param string $uid
 *   User ID.
 */
function drupal_session_destroy_uid($uid) {
  mongodb_collection(variable_get('mongodb_session', 'session'))
    ->remove(array('uid' => $uid));
}

/**
 * Session handler assigned by session_set_save_handler().
 *
 * Cleanup stalled sessions.
 *
 * @param int $lifetime
 *   The value of session.gc_maxlifetime, passed by PHP.
 *   Sessions not updated for more than $lifetime seconds will be removed.
 */
function _drupal_session_garbage_collection($lifetime) {
  // Be sure to adjust 'php_value session.gc_maxlifetime' to a large enough
  // value. For example, if you want user sessions to stay in your database
  // for three weeks before deleting them, you need to set gc_maxlifetime
  // to '1814400'. At that value, only after a user doesn't log in after
  // three weeks (1814400 seconds) will his/her session be removed.
  mongodb_collection(variable_get('mongodb_session', 'session'))
    ->remove(array('timestamp' => array('$lt' => REQUEST_TIME - $lifetime)));
  return TRUE;
}

/**
 * Determine whether to save session data of the current request.
 *
 * This function allows the caller to temporarily disable writing of
 * session data, should the request end while performing potentially
 * dangerous operations, such as manipulating the global $user object.
 * See http://drupal.org/node/218104 for usage.
 *
 * @param $status
 *   Disables writing of session data when FALSE, (re-)enables
 *   writing when TRUE.
 * @return
 *   FALSE if writing session data has been disabled. Otherwise, TRUE.
 */
function drupal_save_session($status = NULL) {
  $save_session = &drupal_static(__FUNCTION__, TRUE);
  if (isset($status)) {
    $save_session = $status;
  }
  return $save_session;
}

}
