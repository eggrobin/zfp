#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define DATA_LEN 1000000
#define RATE_TOL 1e-3

typedef enum {
  FIXED_PRECISION = 1,
  FIXED_RATE = 2,

#ifdef FL_PT_DATA
  FIXED_ACCURACY = 3
#endif

} zfp_mode;

struct setupVars {
  zfp_mode zfpMode;

  Scalar* dataArr;
  Scalar* decompressedArr;

  void* buffer;
  zfp_field* field;
  zfp_field* decompressField;
  zfp_stream* stream;

  // paramNum is 0, 1, or 2
  //   used to compute fixed mode param
  //   and to select proper checksum to compare against
  int paramNum;
  double rateParam;
  int precParam;
  double accParam;

  uint64 compressedChecksums[3];
  UInt decompressedChecksums[3];
};

static int
setupChosenZfpMode(void **state, zfp_mode zfpMode, int paramNum)
{
  struct setupVars *bundle = malloc(sizeof(struct setupVars));
  assert_non_null(bundle);

  bundle->zfpMode = zfpMode;
  bundle->paramNum = paramNum;

  bundle->dataArr = malloc(sizeof(Scalar) * DATA_LEN);
  assert_non_null(bundle->dataArr);

  int dataSideLen = (DIMS == 3) ? 100 : (DIMS == 2) ? 1000 : 1000000;
  switch (ZFP_TYPE) {

#ifdef FL_PT_DATA
    case zfp_type_float:
      generateSmoothRandFloats((float*)bundle->dataArr, dataSideLen, DIMS);
      break;

    case zfp_type_double:
      generateSmoothRandDoubles((double*)bundle->dataArr, dataSideLen, DIMS);
      break;
#else
    case zfp_type_int32:
      generateSmoothRandInts32((int32*)bundle->dataArr, dataSideLen, DIMS, 32 - 2);
      break;

    case zfp_type_int64:
      generateSmoothRandInts64((int64*)bundle->dataArr, dataSideLen, DIMS, 64 - 2);
      break;
#endif

    default:
      fail_msg("Invalid zfp_type during setupChosenZfpMode()");
      break;
  }

  bundle->decompressedArr = malloc(sizeof(Scalar) * DATA_LEN);
  assert_non_null(bundle->decompressedArr);

  zfp_type type = ZFP_TYPE;
  zfp_field* field;
  zfp_field* decompressField;
  switch(DIMS) {
    case 1:
      field = zfp_field_1d(bundle->dataArr, type, 1000000);
      decompressField = zfp_field_1d(bundle->decompressedArr, type, 1000000);
      break;
    case 2:
      field = zfp_field_2d(bundle->dataArr, type, 1000, 1000);
      decompressField = zfp_field_2d(bundle->decompressedArr, type, 1000, 1000);
      break;
    case 3:
      field = zfp_field_3d(bundle->dataArr, type, 100, 100, 100);
      decompressField = zfp_field_3d(bundle->decompressedArr, type, 100, 100, 100);
      break;
  }

  zfp_stream* stream = zfp_stream_open(NULL);

  if (bundle->paramNum > 2 || bundle->paramNum < 0) {
    fail_msg("Unknown paramNum during setupChosenZfpMode()");
  }

  switch(bundle->zfpMode) {
    case FIXED_PRECISION:
      bundle->precParam = 1u << (bundle->paramNum + 3);
      zfp_stream_set_precision(stream, bundle->precParam);
      printf("\t\tFixed precision param: %u\n", bundle->precParam);

      bundle->compressedChecksums[0] = CHECKSUM_FP_COMPRESSED_BITSTREAM_0;
      bundle->compressedChecksums[1] = CHECKSUM_FP_COMPRESSED_BITSTREAM_1;
      bundle->compressedChecksums[2] = CHECKSUM_FP_COMPRESSED_BITSTREAM_2;

      bundle->decompressedChecksums[0] = CHECKSUM_FP_DECOMPRESSED_ARRAY_0;
      bundle->decompressedChecksums[1] = CHECKSUM_FP_DECOMPRESSED_ARRAY_1;
      bundle->decompressedChecksums[2] = CHECKSUM_FP_DECOMPRESSED_ARRAY_2;

      break;

    case FIXED_RATE:
      bundle->rateParam = (double)(1u << (bundle->paramNum + 3));
      zfp_stream_set_rate(stream, bundle->rateParam, type, DIMS, 0);
      printf("\t\tFixed rate param: %lf\n", bundle->rateParam);

      bundle->compressedChecksums[0] = CHECKSUM_FR_COMPRESSED_BITSTREAM_0;
      bundle->compressedChecksums[1] = CHECKSUM_FR_COMPRESSED_BITSTREAM_1;
      bundle->compressedChecksums[2] = CHECKSUM_FR_COMPRESSED_BITSTREAM_2;

      bundle->decompressedChecksums[0] = CHECKSUM_FR_DECOMPRESSED_ARRAY_0;
      bundle->decompressedChecksums[1] = CHECKSUM_FR_DECOMPRESSED_ARRAY_1;
      bundle->decompressedChecksums[2] = CHECKSUM_FR_DECOMPRESSED_ARRAY_2;

      break;

#ifdef FL_PT_DATA
    case FIXED_ACCURACY:
      bundle->accParam = ldexp(1.0, -(1u << bundle->paramNum));
      zfp_stream_set_accuracy(stream, bundle->accParam);
      printf("\t\tFixed accuracy param: %lf\n", bundle->accParam);

      bundle->compressedChecksums[0] = CHECKSUM_FA_COMPRESSED_BITSTREAM_0;
      bundle->compressedChecksums[1] = CHECKSUM_FA_COMPRESSED_BITSTREAM_1;
      bundle->compressedChecksums[2] = CHECKSUM_FA_COMPRESSED_BITSTREAM_2;

      bundle->decompressedChecksums[0] = CHECKSUM_FA_DECOMPRESSED_ARRAY_0;
      bundle->decompressedChecksums[1] = CHECKSUM_FA_DECOMPRESSED_ARRAY_1;
      bundle->decompressedChecksums[2] = CHECKSUM_FA_DECOMPRESSED_ARRAY_2;

      break;
#endif

    default:
      fail_msg("Invalid zfp mode during setupChosenZfpMode()");
      break;
  }

  size_t bufsizeBytes = zfp_stream_maximum_size(stream, field);
  char* buffer = calloc(bufsizeBytes, sizeof(char));
  assert_non_null(buffer);

  bitstream* s = stream_open(buffer, bufsizeBytes);
  assert_non_null(s);

  zfp_stream_set_bit_stream(stream, s);
  zfp_stream_rewind(stream);

  bundle->buffer = buffer;
  bundle->field = field;
  bundle->decompressField = decompressField;
  bundle->stream = stream;
  *state = bundle;

  return 0;
}

static int
setupFixedPrec0(void **state)
{
  setupChosenZfpMode(state, FIXED_PRECISION, 0);
  return 0;
}

static int
setupFixedPrec1(void **state)
{
  setupChosenZfpMode(state, FIXED_PRECISION, 1);
  return 0;
}

static int
setupFixedPrec2(void **state)
{
  setupChosenZfpMode(state, FIXED_PRECISION, 2);
  return 0;
}

static int
setupFixedRate0(void **state)
{
  setupChosenZfpMode(state, FIXED_RATE, 0);
  return 0;
}

static int
setupFixedRate1(void **state)
{
  setupChosenZfpMode(state, FIXED_RATE, 1);
  return 0;
}

static int
setupFixedRate2(void **state)
{
  setupChosenZfpMode(state, FIXED_RATE, 2);
  return 0;
}

#ifdef FL_PT_DATA
static int
setupFixedAccuracy0(void **state)
{
  setupChosenZfpMode(state, FIXED_ACCURACY, 0);
  return 0;
}

static int
setupFixedAccuracy1(void **state)
{
  setupChosenZfpMode(state, FIXED_ACCURACY, 1);
  return 0;
}

static int
setupFixedAccuracy2(void **state)
{
  setupChosenZfpMode(state, FIXED_ACCURACY, 2);
  return 0;
}
#endif

static int
teardown(void **state)
{
  struct setupVars *bundle = *state;
  stream_close(bundle->stream->stream);
  zfp_stream_close(bundle->stream);
  zfp_field_free(bundle->field);
  zfp_field_free(bundle->decompressField);
  free(bundle->buffer);
  free(bundle->dataArr);
  free(bundle->decompressedArr);
  free(bundle);

  return 0;
}

static void
when_seededRandomSmoothDataGenerated_expect_ChecksumMatches(void **state)
{
  struct setupVars *bundle = *state;
  assert_int_equal(hashArray(bundle->dataArr, DATA_LEN, 1), CHECKSUM_ORIGINAL_DATA_ARRAY);
}

static void
assertZfpCompressBitstreamChecksumMatches(void **state)
{
  struct setupVars *bundle = *state;
  zfp_field* field = bundle->field;
  zfp_stream* stream = bundle->stream;
  bitstream* s = zfp_stream_bit_stream(stream);

  zfp_compress(stream, field);

  uint64 checksum = hashBitstream(stream_data(s), stream_size(s));
  uint64 expectedChecksum = bundle->compressedChecksums[bundle->paramNum];

  assert_int_equal(checksum, expectedChecksum);
}

static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpCompressFixedPrecision_expect_BitstreamChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_PRECISION) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressBitstreamChecksumMatches(state);
}

static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpCompressFixedRate_expect_BitstreamChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_RATE) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressBitstreamChecksumMatches(state);
}

#ifdef FL_PT_DATA
static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpCompressFixedAccuracy_expect_BitstreamChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_ACCURACY) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressBitstreamChecksumMatches(state);
}
#endif

static void
assertZfpCompressDecompressChecksumMatches(void **state)
{
  struct setupVars *bundle = *state;
  zfp_field* field = bundle->field;
  zfp_stream* stream = bundle->stream;

  zfp_compress(stream, field);
  zfp_stream_rewind(stream);

  // zfp_decompress() will write to bundle->decompressedArr
  zfp_decompress(stream, bundle->decompressField);

  UInt checksum = hashArray(bundle->decompressedArr, DATA_LEN, 1);
  UInt expectedChecksum = bundle->decompressedChecksums[bundle->paramNum];

  assert_int_equal(checksum, expectedChecksum);
}

static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpDecompressFixedPrecision_expect_ArrayChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_PRECISION) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressDecompressChecksumMatches(state);
}

static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpDecompressFixedRate_expect_ArrayChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_RATE) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressDecompressChecksumMatches(state);
}

#ifdef FL_PT_DATA
static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpDecompressFixedAccuracy_expect_ArrayChecksumMatches)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_ACCURACY) {
    fail_msg("Invalid zfp mode during test");
  }

  assertZfpCompressDecompressChecksumMatches(state);
}
#endif

static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpCompressFixedRate_expect_CompressedBitrateComparableToChosenRate)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_RATE) {
    fail_msg("Test requires fixed rate mode");
  }

  zfp_field* field = bundle->field;
  zfp_stream* stream = bundle->stream;
  bitstream* s = zfp_stream_bit_stream(stream);

  size_t compressedBytes = zfp_compress(stream, field);
  double bitsPerValue = (double)compressedBytes * 8. / DATA_LEN;
  double maxBitrate = bundle->rateParam + RATE_TOL;

  assert_true(bitsPerValue <= maxBitrate);

  printf("\t\tCompressed bitrate: %lf\n", bitsPerValue);
}

#ifdef FL_PT_DATA
static void
_catFunc3(given_, DIM_INT_STR, Array_when_ZfpCompressFixedAccuracy_expect_CompressedValuesWithinAccuracy)(void **state)
{
  struct setupVars *bundle = *state;
  if (bundle->zfpMode != FIXED_ACCURACY) {
    fail_msg("Test requires fixed accuracy mode");
  }

  zfp_field* field = bundle->field;
  zfp_stream* stream = bundle->stream;
  bitstream* s = zfp_stream_bit_stream(stream);

  zfp_compress(stream, field);
  zfp_stream_rewind(stream);

  // zfp_decompress() will write to bundle->decompressedArr
  zfp_decompress(stream, bundle->decompressField);

  float maxDiffF = 0;
  double maxDiffD = 0;
  int i;
  for (i = 0; i < DATA_LEN; i++) {
    float absDiffF;
    double absDiffD;

    switch(ZFP_TYPE) {
      case zfp_type_float:
        absDiffF = fabsf(bundle->decompressedArr[i] - bundle->dataArr[i]);
        assert_true(absDiffF < bundle->accParam);

        if (absDiffF > maxDiffF) {
          maxDiffF = absDiffF;
        }

        break;

      case zfp_type_double:
        absDiffD = fabs(bundle->decompressedArr[i] - bundle->dataArr[i]);
	assert_true(absDiffD < bundle->accParam);

        if (absDiffD > maxDiffD) {
          maxDiffD = absDiffD;
        }

	break;

      default:
        fail_msg("Test requires zfp_type float or double");
    }
  }

  if (ZFP_TYPE == zfp_type_float) {
    printf("\t\tMax abs error: %f\n", maxDiffF);
  } else {
    printf("\t\tMax abs error: %lf\n", maxDiffD);
  }
}
#endif