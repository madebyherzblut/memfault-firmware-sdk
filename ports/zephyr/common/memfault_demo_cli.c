//! @file
//!
//! Copyright (c) Memfault, Inc.
//! See License.txt for details
//!
//! @brief
//! Adds a basic set of commands for interacting with Memfault SDK

#include <shell/shell.h>
#include <stdlib.h>

#include "memfault/components.h"
#include "memfault/ports/zephyr/http.h"
#include "zephyr_release_specific_headers.h"

static int prv_clear_core_cmd(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_cmd_clear_core(argc, argv);
}

static int prv_get_core_cmd(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_cmd_get_core(argc, argv);
}

static int prv_test_log(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_cmd_test_log(argc, argv);
}

static int prv_trigger_logs(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_cmd_trigger_logs(argc, argv);
}

static int prv_get_device_info(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_cmd_get_device_info(argc, argv);
}

//! Route the 'export' command to output via printk, so we don't drop messages
//! from logging a big burst.
void memfault_data_export_base64_encoded_chunk(const char *base64_chunk) {
  printk("%s\n", base64_chunk);
}

static int prv_chunk_data_export(const struct shell *shell, size_t argc, char **argv) {
  memfault_data_export_dump_chunks();
  return 0;
}

static int prv_example_trace_event_capture(const struct shell *shell, size_t argc, char **argv) {
  // For more information on user-defined error reasons, see
  // the MEMFAULT_TRACE_REASON_DEFINE macro in trace_reason_user.h .
  MEMFAULT_TRACE_EVENT_WITH_LOG(MemfaultCli_Test, "Num args: %d", (int)argc);
  MEMFAULT_LOG_DEBUG("Trace Event Generated!");
  return 0;
}

static int prv_post_data(const struct shell *shell, size_t argc, char **argv) {
  // For more information on user-defined error reasons, see
  // the MEMFAULT_TRACE_REASON_DEFINE macro in trace_reason_user.h .
#if defined(CONFIG_MEMFAULT_HTTP_ENABLE)
  MEMFAULT_LOG_INFO("Posting Memfault Data");
  return memfault_zephyr_port_post_data();
#else
  shell_print(shell, "CONFIG_MEMFAULT_HTTP_ENABLE not enabled");
  return 0;
#endif
}

#if defined(CONFIG_MEMFAULT_HTTP_ENABLE)
typedef struct {
  const struct shell *shell;
  size_t total_size;
} sMemfaultShellOtaDownloadCtx;

static bool prv_handle_update_available(const sMemfaultOtaInfo *info, void *user_ctx) {
  sMemfaultShellOtaDownloadCtx *ctx = (sMemfaultShellOtaDownloadCtx *)user_ctx;
  shell_print(ctx->shell, "Downloading OTA payload, size=%d bytes", (int)info->size);
  return true;
}

static bool prv_handle_data(void *buf, size_t buf_len, void *user_ctx) {
  // this is an example cli command so we just drop the data on the floor
  // a real implementation could save the data in this callback!
  return true;
}

static bool prv_handle_download_complete(void *user_ctx) {
  sMemfaultShellOtaDownloadCtx *ctx = (sMemfaultShellOtaDownloadCtx *)user_ctx;
  shell_print(ctx->shell, "OTA download complete!");
  return true;
}
#endif /* CONFIG_MEMFAULT_HTTP_ENABLE */

static int prv_check_and_fetch_ota_payload_cmd(const struct shell *shell, size_t argc,
                                               char **argv) {
#if MEMFAULT_NRF_CONNECT_SDK
  // The nRF Connect SDK comes with a download client module that can be used to
  // perform an actual e2e OTA so use that instead and don't link this code in at all!
  shell_print(shell, "Use 'mflt_nrf fota' instead. See https://mflt.io/nrf-fota-setup for more details");
#elif defined(CONFIG_MEMFAULT_HTTP_ENABLE)
  uint8_t working_buf[256];

  sMemfaultShellOtaDownloadCtx user_ctx = {
    .shell = shell,
  };

  sMemfaultOtaUpdateHandler handler = {
    .buf = working_buf,
    .buf_len = sizeof(working_buf),
    .user_ctx = &user_ctx,
    .handle_update_available = prv_handle_update_available,
    .handle_data = prv_handle_data,
    .handle_download_complete = prv_handle_download_complete,
  };

  shell_print(shell, "Checking for OTA update");
  int rv = memfault_zephyr_port_ota_update(&handler);
  if (rv == 0) {
    shell_print(shell, "Up to date!");
  } else if (rv < 0) {
    shell_print(shell, "OTA update failed, rv=%d, errno=%d", rv, errno);
  }
  return rv;
#else
  shell_print(shell, "CONFIG_MEMFAULT_HTTP_ENABLE not enabled");
  return 0;
#endif
}

static int prv_trigger_heartbeat(const struct shell *shell, size_t argc, char **argv) {
#if CONFIG_MEMFAULT_METRICS
  shell_print(shell, "Triggering Heartbeat");
  memfault_metrics_heartbeat_debug_trigger();
  return 0;
#else
  shell_print(shell, "CONFIG_MEMFAULT_METRICS not enabled");
  return 0;
#endif
}

static int prv_test_reboot(const struct shell *shell, size_t argc, char **argv) {
  memfault_reboot_tracking_mark_reset_imminent(kMfltRebootReason_UserReset, NULL);
  memfault_platform_reboot();
  return 0;  // should be unreachable
}

static int prv_memfault_assert_example(const struct shell *shell, size_t argc, char **argv) {
  memfault_demo_cli_cmd_assert(argc, argv);
  return -1;
}

static int prv_hang_example(const struct shell *shell, size_t argc, char **argv) {
#if !CONFIG_WATCHDOG
  MEMFAULT_LOG_WARN("No watchdog configured, this will hang forever");
#else
  MEMFAULT_LOG_DEBUG("Hanging system and waiting for watchdog!");
#endif
  while (1) {
  }
  return -1;
}

static int prv_busfault_example(const struct shell *shell, size_t argc, char **argv) {
  //! Note: The Zephyr fault handler dereferences the pc which triggers a fault
  //! if the pc itself is from a bad pointer: https://github.com/zephyrproject-rtos/zephyr/blob/f400c94/arch/arm/core/aarch32/cortex_m/fault.c#L664
  //!
  //! We set the BFHFNMIGN bit to prevent a lockup from happening due to de-referencing the bad PC
  //! which generated the fault in the first place
  volatile uint32_t *ccr = (uint32_t *)0xE000ED14;
  *ccr |= 0x1 << 8;

  memfault_demo_cli_cmd_busfault(argc, argv);
  return -1;
}

static int prv_hardfault_example(const struct shell *shell, size_t argc, char **argv) {
  memfault_demo_cli_cmd_hardfault(argc, argv);
  return -1;
}

static int prv_usagefault_example(const struct shell *shell, size_t argc, char **argv) {
  memfault_demo_cli_cmd_usagefault(argc, argv);
  return -1;
}

static int prv_memmanage_example(const struct shell *shell, size_t argc, char **argv) {
  memfault_demo_cli_cmd_memmanage(argc, argv);
  return -1;
}

static int prv_zephyr_assert_example(const struct shell *shell, size_t argc, char **argv) {
#if !CONFIG_ASSERT
  MEMFAULT_LOG_WARN("CONFIG_ASSERT was disabled in the build, this command will have no effect");
#endif
  __ASSERT(0, "test assert");
  return 0;
}

static int prv_zephyr_load_32bit_address(const struct shell *shell, size_t argc, char **argv) {
  return memfault_demo_cli_loadaddr(argc, argv);
}

static int prv_cli_cmd_double_free(const struct shell *shell, size_t argc, char **argv) {
  (void)shell;
  (void)argc;
  (void)argv;

#if !CONFIG_MEMFAULT_HEAP_STATS
  MEMFAULT_LOG_WARN(
    "CONFIG_MEMFAULT_HEAP_STATS was disabled in the build, this command will have no effect");
#else
  uint8_t *ptr = k_malloc(100);
  k_free(ptr);
  k_free(ptr);

  shell_print(shell, "Double free should have crashed the app! 🤯 Make sure CONFIG_ASSERT=y");
#endif

  return -1;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
  sub_memfault_crash_cmds,
  //! different crash types that should result in a coredump being collected
  SHELL_CMD(assert, NULL, "trigger memfault assert", prv_memfault_assert_example),
  SHELL_CMD(busfault, NULL, "trigger a busfault", prv_busfault_example),
  SHELL_CMD(hang, NULL, "trigger a hang", prv_hang_example),
  SHELL_CMD(hardfault, NULL, "trigger a hardfault", prv_hardfault_example),
  SHELL_CMD(memmanage, NULL, "trigger a memory management fault", prv_memmanage_example),
  SHELL_CMD(usagefault, NULL, "trigger a usage fault", prv_usagefault_example),
  SHELL_CMD(zassert, NULL, "trigger a zephyr assert", prv_zephyr_assert_example),
  SHELL_CMD(loadaddr, NULL, "test a 32 bit load from an address", prv_zephyr_load_32bit_address),
  SHELL_CMD(double_free, NULL, "Trigger a double free error", prv_cli_cmd_double_free),

  //! user initiated reboot
  SHELL_CMD(reboot, NULL, "trigger a reboot and record it using memfault", prv_test_reboot),

  //! memfault data source test commands
  SHELL_CMD(heartbeat, NULL, "trigger an immediate capture of all heartbeat metrics",
            prv_trigger_heartbeat),
  SHELL_CMD(log_capture, NULL, "trigger capture of current log buffer contents", prv_trigger_logs),
  SHELL_CMD(logs, NULL, "writes test logs to log buffer", prv_test_log),
  SHELL_CMD(trace, NULL, "capture an example trace event", prv_example_trace_event_capture),

  SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(
  sub_memfault_cmds, SHELL_CMD(clear_core, NULL, "clear coredump collected", prv_clear_core_cmd),
  SHELL_CMD(export, NULL,
            "dump chunks collected by Memfault SDK using https://mflt.io/chunk-data-export",
            prv_chunk_data_export),
  SHELL_CMD(get_core, NULL, "check if coredump is stored and present", prv_get_core_cmd),
  SHELL_CMD(get_device_info, NULL, "display device information", prv_get_device_info),
  SHELL_CMD(get_latest_release, NULL, "checks to see if new ota payload is available",
            prv_check_and_fetch_ota_payload_cmd),
  SHELL_CMD(post_chunks, NULL, "Post Memfault data to cloud", prv_post_data),
  SHELL_CMD(test, &sub_memfault_crash_cmds,
            "commands to verify memfault data collection (https://mflt.io/mcu-test-commands)",
            NULL),
  SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(mflt, &sub_memfault_cmds, "Memfault Test Commands", NULL);
