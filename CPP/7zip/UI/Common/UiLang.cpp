// UiLang.cpp - Built-in auto-switching zh-CN / en-US UI localization (ASCII-safe)

#include "StdAfx.h"

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"
#include "UiLang.h"

extern UString g_LangID;

namespace NUiLang {

bool IsZhCn()
{
  if (g_LangID.Len() == 1 && g_LangID[0] == L'-')
    return false;
  #ifdef _WIN32
  return PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_CHINESE;
  #else
  return false;
  #endif
}

struct CBuiltinLangEntry
{
  UInt32 Id;
  const wchar_t *Text;
};

static const CBuiltinLangEntry kZhCnTable[] =
{
  {  401u, L"\x786E\x5B9A" },
  {  402u, L"\x53D6\x6D88" },
  {  406u, L"\x662F(&Y)" },
  {  407u, L"\x5426(&N)" },
  {  408u, L"\x5173\x95ED" },
  {  409u, L"\x5E2E\x52A9" },
  {  411u, L"\x7EE7\x7EED(&C)" },
  {  444u, L"\x540E\x53F0(&B)" },
  {  445u, L"\x524D\x53F0(&F)" },
  {  446u, L"\x6682\x505C(&P)" },
  {  447u, L"\x5DF2\x6682\x505C" },
  {  448u, L"\x786E\x5B9A\x8981\x53D6\x6D88\x64CD\x4F5C\x5417\xFF1F" },
  {  500u, L"\x6587\x4EF6(&F)" },
  {  501u, L"\x7F16\x8F91(&E)" },
  {  502u, L"\x67E5\x770B(&V)" },
  {  503u, L"\x6536\x85CF(&A)" },
  {  504u, L"\x5DE5\x5177(&T)" },
  {  505u, L"\x5E2E\x52A9(&H)" },
  {  540u, L"\x6253\x5F00(&O)\tEnter" },
  {  541u, L"\x5185\x90E8\x6253\x5F00(&I)\tCtrl+PgDn" },
  {  542u, L"\x5916\x90E8\x6253\x5F00(&U)\tShift+Enter" },
  {  543u, L"\x67E5\x770B(&V)\tF3" },
  {  544u, L"\x7F16\x8F91(&E)\tF4" },
  {  545u, L"\x91CD\x547D\x540D(&M)\tF2" },
  {  546u, L"\x590D\x5236\x5230(&C)...\tF5" },
  {  547u, L"\x79FB\x52A8\x5230(&M)...\tF6" },
  {  548u, L"\x5220\x9664(&D)\tDel" },
  {  549u, L"\x5206\x5377\x5206\x5272(&S)..." },
  {  550u, L"\x5408\x5E76\x6587\x4EF6(&B)..." },
  {  551u, L"\x5C5E\x6027(&R)\tAlt+Enter" },
  {  552u, L"\x6CE8\x91CA(&N)...\tCtrl+Z" },
  {  553u, L"\x8BA1\x7B97\x6821\x9A8C\x548C" },
  {  554u, L"\x6587\x4EF6\x5BF9\x6BD4(&F)" },
  {  555u, L"\x65B0\x5EFA\x6587\x4EF6\x5939\tF7" },
  {  556u, L"\x65B0\x5EFA\x6587\x4EF6\tCtrl+N" },
  {  557u, L"\x9000\x51FA(&X)\tAlt+F4" },
  {  558u, L"\x521B\x5EFA\x94FE\x63A5(&L)..." },
  {  559u, L"\x5907\x7528\x6570\x636E\x6D41(&A)" },
  {  590u, L"\x5185\x90E8\x6253\x5F00 *" },
  {  591u, L"\x5185\x90E8\x6253\x5F00 #" },
  {  600u, L"\x5168\x9009(&A)\tCtrl+A" },
  {  601u, L"\x53D6\x6D88\x5168\x9009" },
  {  602u, L"\x53CD\x9009(&I)" },
  {  603u, L"\x6309\x901A\x914D\x7B26\x9009\x62E9..." },
  {  604u, L"\x6309\x901A\x914D\x7B26\x53D6\x6D88\x9009\x62E9..." },
  {  605u, L"\x6309\x7C7B\x578B\x9009\x62E9" },
  {  606u, L"\x6309\x7C7B\x578B\x53D6\x6D88\x9009\x62E9" },
  {  700u, L"\x5927\x56FE\x6807(&G)\tCtrl+1" },
  {  701u, L"\x5C0F\x56FE\x6807(&M)\tCtrl+2" },
  {  702u, L"\x5217\x8868(&L)\tCtrl+3" },
  {  703u, L"\x8BE6\x7EC6\x4FE1\x606F(&D)\tCtrl+4" },
  {  710u, L"\x6309\x540D\x79F0\x6392\x5E8F\tCtrl+F3" },
  {  711u, L"\x6309\x7C7B\x578B\x6392\x5E8F\tCtrl+F4" },
  {  712u, L"\x6309\x65E5\x671F\x6392\x5E8F\tCtrl+F5" },
  {  713u, L"\x6309\x5927\x5C0F\x6392\x5E8F\tCtrl+F6" },
  {  730u, L"\x4E0D\x6392\x5E8F\tCtrl+F7" },
  {  731u, L"\x5E73\x94FA\x89C6\x56FE" },
  {  732u, L"\x53CC\x9762\x677F(&2)\tF9" },
  {  733u, L"\x5DE5\x5177\x680F(&T)" },
  {  734u, L"\x6253\x5F00\x6839\x6587\x4EF6\x5939\t\\" },
  {  735u, L"\x4E0A\x7EA7\x6587\x4EF6\x5939\tBackspace" },
  {  736u, L"\x6587\x4EF6\x5939\x5386\x53F2\x8BB0\x5F55...\tAlt+F12" },
  {  737u, L"\x5237\x65B0(&R)\tCtrl+R" },
  {  738u, L"\x81EA\x52A8\x5237\x65B0" },
  {  750u, L"\x538B\x7F29\x5305\x5DE5\x5177\x680F" },
  {  751u, L"\x6807\x51C6\x5DE5\x5177\x680F" },
  {  752u, L"\x5927\x56FE\x6807\x6309\x94AE" },
  {  753u, L"\x663E\x793A\x6309\x94AE\x6587\x5B57" },
  {  800u, L"\x6DFB\x52A0\x5230\x6536\x85CF\x5939(&A)" },
  {  801u, L"\x6536\x85CF\x5939" },
  {  900u, L"\x8BBE\x7F6E(&O)..." },
  {  901u, L"\x57FA\x51C6\x6D4B\x8BD5(&B)" },
  {  902u, L"\x57FA\x51C6\x6D4B\x8BD5 (2)" },
  {  910u, L"\x4E34\x65F6\x6587\x4EF6\x5939" },
  {  960u, L"\x5E2E\x52A9\x5185\x5BB9(&C)\tF1" },
  {  961u, L"\x5173\x4E8E UnlockZip(&A)..." },
  { 1003u, L"\x8DEF\x5F84" },
  { 1004u, L"\x540D\x79F0" },
  { 1005u, L"\x6269\x5C55\x540D" },
  { 1006u, L"\x6587\x4EF6\x5939" },
  { 1007u, L"\x5927\x5C0F" },
  { 1008u, L"\x538B\x7F29\x540E\x5927\x5C0F" },
  { 1009u, L"\x5C5E\x6027" },
  { 1010u, L"\x521B\x5EFA\x65F6\x95F4" },
  { 1011u, L"\x8BBF\x95EE\x65F6\x95F4" },
  { 1012u, L"\x4FEE\x6539\x65F6\x95F4" },
  { 1013u, L"\x56FA\x5B9E" },
  { 1014u, L"\x6709\x6CE8\x91CA" },
  { 1015u, L"\x52A0\x5BC6" },
  { 1018u, L"\x5B57\x5178\x5927\x5C0F" },
  { 1019u, L"CRC" },
  { 1020u, L"\x7C7B\x578B" },
  { 1022u, L"\x538B\x7F29\x7B97\x6CD5" },
  { 1023u, L"\x4E3B\x673A\x7CFB\x7EDF" },
  { 1028u, L"\x6CE8\x91CA" },
  { 1031u, L"\x6587\x4EF6\x5939\x6570" },
  { 1032u, L"\x6587\x4EF6\x6570" },
  { 1056u, L"\x603B\x5927\x5C0F" },
  { 1057u, L"\x53EF\x7528\x7A7A\x95F4" },
  { 2100u, L"\x8BBE\x7F6E" },
  { 2320u, L"<\x6587\x4EF6\x5939>" },
  { 2321u, L"<\x538B\x7F29\x5305>" },
  { 2322u, L"\x6253\x5F00\x538B\x7F29\x5305" },
  { 2323u, L"\x89E3\x538B\x6587\x4EF6..." },
  { 2324u, L"\x6DFB\x52A0\x5230\x538B\x7F29\x5305..." },
  { 2325u, L"\x6D4B\x8BD5\x538B\x7F29\x5305" },
  { 2326u, L"\x89E3\x538B\x5230\x5F53\x524D\x6587\x4EF6\x5939" },
  { 2327u, L"\x89E3\x538B\x5230 \"{0}\"" },
  { 2328u, L"\x6DFB\x52A0\x5230 \"{0}\"" },
  { 2329u, L"\x538B\x7F29\x5E76\x53D1\x9001\x90AE\x4EF6..." },
  { 2330u, L"\x538B\x7F29\x4E3A \"{0}\" \x5E76\x53D1\x9001\x90AE\x4EF6" },
  { 2500u, L"\x5E38\x89C4\x8BBE\x7F6E" },
  { 2501u, L"\x663E\x793A \"..\" \x8FD4\x56DE\x4E0A\x7EA7\x9879" },
  { 2502u, L"\x663E\x793A\x771F\x5B9E\x6587\x4EF6\x56FE\x6807(&I)" },
  { 2503u, L"\x663E\x793A\x7CFB\x7EDF\x53F3\x952E\x83DC\x5355(&M)" },
  { 2504u, L"\x6574\x884C\x9009\x4E2D(&F)" },
  { 2505u, L"\x663E\x793A\x7F51\x683C\x7EBF(&G)" },
  { 2506u, L"\x5355\x51FB\x6253\x5F00\x9879\x76EE(&S)" },
  { 2507u, L"\x5907\x7528\x9009\x62E9\x6A21\x5F0F(&A)" },
  { 2508u, L"\x4F7F\x7528\x5927\x5185\x5B58\x9875(&L)" },
  { 2509u, L"\x8BB0\x4F4F\x538B\x7F29\x5305\x5386\x53F2\x8BB0\x5F55" },
  { 2510u, L"\x8BB0\x4F4F\x8DEF\x5F84\x5386\x53F2\x8BB0\x5F55" },
  { 2511u, L"\x8BB0\x4F4F\x590D\x5236\x8DEF\x5F84\x5386\x53F2" },
  { 2512u, L"\x8BB0\x4F4F\x6587\x4EF6\x5939\x5386\x53F2\x8BB0\x5F55" },
  { 2513u, L"\x4F7F\x7528\x5C0F\x5199\x54C8\x5E0C\x503C" },
  { 2514u, L"\x81EA\x52A8\x5C1D\x8BD5\x67E5\x627E\x538B\x7F29\x5305\x5BC6\x7801 (PwFree)" },
  { 2515u, L"\x5141\x8BB8\x67E5\x8BE2\x4E91\x7AEF\x5BC6\x7801\x5E93" },
  { 2516u, L"\x5728\x72B6\x6001\x680F\x663E\x793A\x5BC6\x7801\x6765\x6E90" },
  { 2517u, L"\x5141\x8BB8\x4F7F\x7528\x672C\x5730\x79BB\x7EBF\x5B57\x5178" },
  { 3002u, L"\x5DF2\x9009 {0} / \x5171 {1} \x9879" },
  { 3014u, L"\x8BF7\x5148\x9009\x62E9\x4E00\x4E2A\x6587\x4EF6" },
  { 3015u, L"\x8BF7\x81F3\x5C11\x9009\x62E9\x4E00\x4E2A\x6587\x4EF6" },
  { 3104u, L"\x5916\x89C2\x4E3B\x9898:" },
  { 3800u, L"\x8F93\x5165\x5BC6\x7801" },
  { 3801u, L"\x8BF7\x8F93\x5165\x89E3\x538B\x5BC6\x7801:" },
  { 3803u, L"\x663E\x793A\x5BC6\x7801(&S)" },
  { 3900u, L"\x5DF2\x7528\x65F6\x95F4:" },
  { 3901u, L"\x5269\x4F59\x65F6\x95F4:" },
  { 3902u, L"\x603B\x5927\x5C0F:" },
  { 3903u, L"\x901F\x5EA6:" },
  { 3904u, L"\x5DF2\x5904\x7406:" },
  { 3905u, L"\x538B\x7F29\x7387:" },
  { 3906u, L"\x9519\x8BEF:" },
  { 6000u, L"\x590D\x5236" },
  { 6001u, L"\x79FB\x52A8" },
  { 6002u, L"\x590D\x5236\x5230:" },
  { 6003u, L"\x79FB\x52A8\x5230:" },
  { 6004u, L"\x6B63\x5728\x590D\x5236..." },
  { 6006u, L"\x6B63\x5728\x91CD\x547D\x540D..." },
  { 6100u, L"\x786E\x8BA4\x5220\x9664\x6587\x4EF6" },
  { 6101u, L"\x786E\x8BA4\x5220\x9664\x6587\x4EF6\x5939" },
  { 6102u, L"\x786E\x8BA4\x5220\x9664\x591A\x9879" },
  { 6106u, L"\x6B63\x5728\x5220\x9664..." },
  { 6300u, L"\x65B0\x5EFA\x6587\x4EF6\x5939" },
  { 6301u, L"\x65B0\x5EFA\x6587\x4EF6" },
  { 6302u, L"\x8BF7\x8F93\x5165\x6587\x4EF6\x5939\x540D\x79F0:" },
  { 6303u, L"\x8BF7\x8F93\x5165\x6587\x4EF6\x540D\x79F0:" },
  { 6600u, L"\x5C5E\x6027" },
  { 6601u, L"\x5386\x53F2\x6587\x4EF6\x5939" },
  { 7100u, L"\x6B64\x7535\x8111" },
  { 7101u, L"\x7F51\x7EDC" },
  { 7102u, L"\x6587\x6863" },
  { 7103u, L"\x7CFB\x7EDF" },
  { 7200u, L"\x6DFB\x52A0" },
  { 7201u, L"\x89E3\x538B" },
  { 7202u, L"\x6D4B\x8BD5" },
  { 7203u, L"\x590D\x5236" },
  { 7204u, L"\x79FB\x52A8" },
  { 7205u, L"\x5220\x9664" },
  { 7206u, L"\x4FE1\x606F" },
  { 7816u, L"\x89E3\x538B\x5141\x8BB8\x4F7F\x7528\x7684\x6700\x5927\x5185\x5B58:" },
};

const wchar_t *LookupBuiltinZhCn(UInt32 id)
{
  unsigned left = 0;
  unsigned right = (unsigned)(sizeof(kZhCnTable) / sizeof(kZhCnTable[0]));
  while (left < right)
  {
    const unsigned mid = left + (right - left) / 2;
    const UInt32 midId = kZhCnTable[mid].Id;
    if (midId == id)
      return kZhCnTable[mid].Text;
    if (midId < id)
      left = mid + 1;
    else
      right = mid;
  }
  return NULL;
}

} // namespace NUiLang
