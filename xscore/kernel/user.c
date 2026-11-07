    /* Verify CRC checksum utility cksum */
    const char *cksum_args[] = {"/bin/cksum", "--help", 0};
    struct xscore_process *p_cksum = xscore_process_create_path_args("/bin/cksum", 2, cksum_args);
    uint64_t cksum_status = UINT64_MAX;
    if (!p_cksum || !xscore_process_wait(p_cksum, &cksum_status) || cksum_status != 0 || !xscore_process_reap(p_cksum)) return 0;
    xscore_serial_write("[INFO] CRC checksum utility cksum verified\n");

    /* Verify terminal clearer clear */
    const char *clear_args[] = {"/bin/clear", "--help", 0};
    struct xscore_process *p_clear = xscore_process_create_path_args("/bin/clear", 2, clear_args);
    uint64_t clear_status = UINT64_MAX;
    if (!p_clear || !xscore_process_wait(p_clear, &clear_status) || clear_status != 0 || !xscore_process_reap(p_clear)) return 0;
    xscore_serial_write("[INFO] Terminal clearer clear verified\n");
