#ifndef OTA_META_H
#define OTA_META_H

#include <stdint.h>

#define METADATA_MAGIC   0xDEADBEEFUL
/* Slot lifecycle (§7.3.1): INVALID → UPDATING → UPDATED → TRIAL → CONFIRMED */
#define SLOT_CONFIRMED   0xAAAAAAAAUL   /* self-test 통과, known-good */
#define SLOT_UPDATED     0xBBBBBBBBUL   /* 기록+정적검증 완료, 첫 시험부팅 대기 (구 PENDING) */
#define SLOT_INVALID     0xCCCCCCCCUL   /* 무효 / 3-strike 실패 마킹 */
#define SLOT_UPDATING    0xDDDDDDDDUL   /* 기록 진행 중(erase/program) */
#define SLOT_TRIAL       0xEEEEEEEEUL   /* 시험부팅됨(attempt 카운트), self-test 대기 */

#define METADATA_A_ADDR  0x08008000UL   /* sector 2 — metadata copy A */
#define METADATA_B_ADDR  0x0800C000UL   /* sector 3 — metadata copy B */

/* Redundant Boot Metadata (FR-AB-005): two copies in flash sectors 2 & 3.
 * - crc32 is the LAST field, written last → a torn (power-loss) copy fails CRC
 *   and is rejected, so the other copy stays the source of truth (atomic commit).
 * - seq_counter is monotonic; the valid copy with the highest seq is "current". */
typedef struct {
    uint32_t magic;
    uint32_t seq_counter;     /* monotonic; higher = newer */
    uint32_t active_slot;     /* 0 = Slot A, 1 = Slot B */
    uint32_t slot_a_status;
    uint32_t slot_b_status;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_count;
    uint32_t slot_a_size;
    uint32_t slot_b_size;
    uint32_t crc32;           /* CRC-32 over all preceding fields */
} OTA_Metadata_t;

/* CRC-32 (zlib / ISO-HDLC: poly 0xEDB88320, init/xorout 0xFFFFFFFF). */
uint32_t crc32_compute(const uint8_t *data, uint32_t len);

/* CRC over the metadata struct excluding its trailing crc32 field. */
uint32_t ota_meta_crc(const OTA_Metadata_t *m);

/* 1 if magic and crc32 are valid, else 0. */
int      ota_meta_valid(const OTA_Metadata_t *m);

/* Select the valid copy with the highest seq into *out.
 * Returns 1 if at least one copy is valid, else 0 (caller → safe state). */
int      ota_meta_select(const OTA_Metadata_t *a, const OTA_Metadata_t *b, OTA_Metadata_t *out);

/* ── 부팅 시 생명주기 결정 (FR-AB-007: trial + 3-strike 롤백) ── */
typedef enum {
    BOOT_EV_CONFIRMED = 0,   /* CONFIRMED 슬롯 그대로 부팅 */
    BOOT_EV_TRIAL_START,     /* UPDATED→TRIAL 첫 시험부팅 */
    BOOT_EV_TRIAL_RETRY,     /* TRIAL 재시도(attempt++) */
    BOOT_EV_ROLLBACK,        /* 3-strike: 후보 INVALID + 이전 CONFIRMED 롤백/safe */
    BOOT_EV_FALLBACK,        /* 후보 무효 → 반대 CONFIRMED 폴백 */
    BOOT_EV_FACTORY,         /* 메타 없음 → Slot A 기본 */
    BOOT_EV_SAFE             /* 가용 슬롯 없음 → safe state */
} OTA_BootEvent_t;

typedef struct {
    int write;             /* 1 → 점프 전에 *out을 플래시에 기록해야 함 */
    int boot_slot;         /* 0=Slot A, 1=Slot B, -1=safe state */
    OTA_BootEvent_t event; /* 진단/로깅용 — 어떤 결정이었는지 */
} OTA_BootPlan_t;

/* 현재 메타(in)로부터 부팅 슬롯과, 점프 전 커밋할 메타(*out)를 결정한다(순수).
 * UPDATED→TRIAL(count=1), TRIAL→count++ 또는 3-strike 시 INVALID+롤백. */
OTA_BootPlan_t ota_meta_plan_boot(const OTA_Metadata_t *in, OTA_Metadata_t *out, uint32_t max_attempts);

/* App self-test 통과 → my_slot 확정(TRIAL→CONFIRMED, attempt=0). 기록 필요 시 1 반환. */
int ota_meta_plan_confirm(const OTA_Metadata_t *in, uint32_t my_slot, OTA_Metadata_t *out);

/* ── 서명 이미지 헤더 + anti-rollback (FR-AB-008 / FR-BL-008, ADR-007) ──
 * 서명 이미지 레이아웃(앞 헤더, MCUboot 정석):
 *   [ 헤더(IMG_HEADER_SIZE) ][ 벡터테이블+코드 ][ ECDSA 서명 64B ]
 * 헤더가 슬롯 맨 앞(offset 0). ECDSA가 (헤더+코드)를 덮어 fw_version 위조 불가.
 * 앱 벡터테이블은 slot+IMG_HEADER_SIZE → 부트로더가 그 주소로 점프(VTOR 설정). */
#define IMG_HEADER_MAGIC 0x4F544148UL   /* 'OTAH' */
#define IMG_HEADER_SIZE  0x200u         /* 헤더 예약영역(VTOR 0x200 정렬); 앱은 +0x200 */
#define ECDSA_SIG_LEN    64u
typedef struct {
    uint32_t magic;          /* = IMG_HEADER_MAGIC */
    uint32_t fw_version;     /* anti-rollback 비교 대상 */
    uint32_t target_ecu_id;  /* 1=DRIVE, 2=SENSOR (선택 검증) */
    uint32_t reserved;
} OTA_ImgHeader_t;           /* 16 bytes (나머지 IMG_HEADER_SIZE는 패딩) */

/* 이미지 맨 앞(offset 0)의 헤더를 읽는다. magic 유효 시 1. */
int ota_img_header_read(const uint8_t *image_start, OTA_ImgHeader_t *out);

/* anti-rollback(FR-BL-008): incoming_version이 CONFIRMED 슬롯들의 최고 버전(기준선)
 * 이상이면 1(허용), 낮으면 0(다운그레이드 거부). 기준선은 메타 NV(CRC) — ADR-007 한계. */
int ota_meta_version_allowed(const OTA_Metadata_t *m, uint32_t incoming_version);

#endif /* OTA_META_H */
