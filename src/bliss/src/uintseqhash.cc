#include "bliss/uintseqhash.hh"

/*
  Copyright (c) 2003-2015 Tommi Junttila
  Released under the GNU Lesser General Public License version 3.

  This file is part of bliss.

  bliss is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, version 3 of the License.

  bliss is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bliss.  If not, see <http://www.gnu.org/licenses/>.
*/

namespace bliss {

/*
 * Random bits generated by
 * http://www.fourmilab.ch/hotbits/
 */
static unsigned int rtab[256] = {
  0xAEAA35B8, 0x65632E16, 0x155EDBA9, 0x01349B39,
  0x8EB8BD97, 0x8E4C5367, 0x8EA78B35, 0x2B1B4072,
  0xC1163893, 0x269A8642, 0xC79D7F6D, 0x6A32DEA0,
  0xD4D2DA56, 0xD96D4F47, 0x47B5F48A, 0x2587C6BF,
  0x642B71D8, 0x5DBBAF58, 0x5C178169, 0xA16D9279,
  0x75CDA063, 0x291BC48B, 0x01AC2F47, 0x5416DF7C,
  0x45307514, 0xB3E1317B, 0xE1C7A8DE, 0x3ACDAC96,
  0x11B96831, 0x32DE22DD, 0x6A1DA93B, 0x58B62381,
  0x283810E2, 0xBC30E6A6, 0x8EE51705, 0xB06E8DFB,
  0x729AB12A, 0xA9634922, 0x1A6E8525, 0x49DD4E19,
  0xE5DB3D44, 0x8C5B3A02, 0xEBDE2864, 0xA9146D9F,
  0x736D2CB4, 0xF5229F42, 0x712BA846, 0x20631593,
  0x89C02603, 0xD5A5BF6A, 0x823F4E18, 0x5BE5DEFF,
  0x1C4EBBFA, 0x5FAB8490, 0x6E559B0C, 0x1FE528D6,
  0xB3198066, 0x4A965EB5, 0xFE8BB3D5, 0x4D2F6234,
  0x5F125AA4, 0xBCC640FA, 0x4F8BC191, 0xA447E537,
  0xAC474D3C, 0x703BFA2C, 0x617DC0E7, 0xF26299D7,
  0xC90FD835, 0x33B71C7B, 0x6D83E138, 0xCBB1BB14,
  0x029CF5FF, 0x7CBD093D, 0x4C9825EF, 0x845C4D6D,
  0x124349A5, 0x53942D21, 0x800E60DA, 0x2BA6EB7F,
  0xCEBF30D3, 0xEB18D449, 0xE281F724, 0x58B1CB09,
  0xD469A13D, 0x9C7495C3, 0xE53A7810, 0xA866C08E,
  0x832A038B, 0xDDDCA484, 0xD5FE0DDE, 0x0756002B,
  0x2FF51342, 0x60FEC9C8, 0x061A53E3, 0x47B1884E,
  0xDC17E461, 0xA17A6A37, 0x3158E7E2, 0xA40D873B,
  0x45AE2140, 0xC8F36149, 0x63A4EE2D, 0xD7107447,
  0x6F90994F, 0x5006770F, 0xC1F3CA9A, 0x91B317B2,
  0xF61B4406, 0xA8C9EE8F, 0xC6939B75, 0xB28BBC3B,
  0x36BF4AEF, 0x3B12118D, 0x4D536ECF, 0x9CF4B46B,
  0xE8AB1E03, 0x8225A360, 0x7AE4A130, 0xC4EE8B50,
  0x50651797, 0x5BB4C59F, 0xD120EE47, 0x24F3A386,
  0xBE579B45, 0x3A378EFC, 0xC5AB007B, 0x3668942B,
  0x2DBDCC3A, 0x6F37F64C, 0xC24F862A, 0xB6F97FCF,
  0x9E4FA23D, 0x551AE769, 0x46A8A5A6, 0xDC1BCFDD,
  0x8F684CF9, 0x501D811B, 0x84279F80, 0x2614E0AC,
  0x86445276, 0xAEA0CE71, 0x0812250F, 0xB586D18A,
  0xC68D721B, 0x44514E1D, 0x37CDB99A, 0x24731F89,
  0xFA72E589, 0x81E6EBA2, 0x15452965, 0x55523D9D,
  0x2DC47E14, 0x2E7FA107, 0xA7790F23, 0x40EBFDBB,
  0x77E7906B, 0x6C1DB960, 0x1A8B9898, 0x65FA0D90,
  0xED28B4D8, 0x34C3ED75, 0x768FD2EC, 0xFAB60BCB,
  0x962C75F4, 0x304F0498, 0x0A41A36B, 0xF7DE2A4A,
  0xF4770FE2, 0x73C93BBB, 0xD21C82C5, 0x6C387447,
  0x8CDB4CB9, 0x2CC243E8, 0x41859E3D, 0xB667B9CB,
  0x89681E8A, 0x61A0526C, 0x883EDDDC, 0x539DE9A4,
  0xC29E1DEC, 0x97C71EC5, 0x4A560A66, 0xBD7ECACF,
  0x576AE998, 0x31CE5616, 0x97172A6C, 0x83D047C4,
  0x274EA9A8, 0xEB31A9DA, 0x327209B5, 0x14D1F2CB,
  0x00FE1D96, 0x817DBE08, 0xD3E55AED, 0xF2D30AFC,
  0xFB072660, 0x866687D6, 0x92552EB9, 0xEA8219CD,
  0xF7927269, 0xF1948483, 0x694C1DF5, 0xB7D8B7BF,
  0xFFBC5D2F, 0x2E88B849, 0x883FD32B, 0xA0331192,
  0x8CB244DF, 0x41FAF895, 0x16902220, 0x97FB512A,
  0x2BEA3CC4, 0xAF9CAE61, 0x41ACD0D5, 0xFD2F28FF,
  0xE780ADFA, 0xB3A3A76E, 0x7112AD87, 0x7C3D6058,
  0x69E64FFF, 0xE5F8617C, 0x8580727C, 0x41F54F04,
  0xD72BE498, 0x653D1795, 0x1275A327, 0x14B499D4,
  0x4E34D553, 0x4687AA39, 0x68B64292, 0x5C18ABC3,
  0x41EABFCC, 0x92A85616, 0x82684CF8, 0x5B9F8A4E,
  0x35382FFE, 0xFB936318, 0x52C08E15, 0x80918B2E,
  0x199EDEE0, 0xA9470163, 0xEC44ACDD, 0x612D6735,
  0x8F88EA7D, 0x759F5EA4, 0xE5CC7240, 0x68CFEB8B,
  0x04725601, 0x0C22C23E, 0x5BC97174, 0x89965841,
  0x5D939479, 0x690F338A, 0x3C2D4380, 0xDAE97F2B
};


void UintSeqHash::update(unsigned int i)
{
  i++;
  while(i > 0)
    {
      h ^= rtab[i & 0xff];
#if 1
      const unsigned int b = (h & 0x80000000) >> 31;
      i = i >> 8;
      h = (h << 1) | b;
#else
      const unsigned int b = h & 0x80000000;
      h = h << 1;
      if(b != 0)
	h++;
      i = i >> 8;
#endif
    }
}


} // namespace bliss
