# Grassland 에셋 세트 (테마별 PCG 에셋)

`generate_grassland_set.py`가 공용 키트 `../common/meshkit.py` 위에서 초원 테마 메시 11개를 절차적으로 만들어
`meshes/*.glb`(glTF 2.0 바이너리, 텍스처 내장)로 쓰고, 자체 검증 후 `preview.webp`(컨택트 시트)와
`textures_preview.webp`(머티리얼별 baseColor·alpha 띠)를 그린다. 의존성은 Python 3 + numpy + matplotlib + Pillow뿐이다.

```
python3 generate_grassland_set.py
```

단위: 미터, glTF 규격대로 Y-up, 피벗은 바닥 중앙(y=0). 언리얼 Interchange 가져오기가 cm·Z-up으로 변환한다.
미리보기는 `.png`가 아니라 `.webp`다(클라이언트 저장소가 `*.png`를 git-lfs로 추적하기 때문). 텍스처 PNG는 `.glb` 안에만 들어 있고 낱개 파일로는 없다.

품질 사양(키트 공통):
- 스무스 셰이딩: 꼭짓점 노멀 평균, 주름각 50° 초과(바위 모서리·캡 가장자리)만 하드 엣지.
- 머티리얼마다 512² baseColor + 탄젠트 공간 노멀맵(높이장 → 중앙차분)을 BIN 청크에 내장. TEXCOORD_0 전 메시. TANGENT는 넣지 않음(언리얼이 MikkTSpace로 계산).
- 잎·풀·꽃·클로버는 두꺼운 튜브 대신 알파 컷아웃 카드(alphaMode MASK, alphaCutoff 0.5, doubleSided). 교차·휜 배열이라 어느 각도에서도 읽힌다.

이 테마만의 추가(스크립트 안에 정의, 키트는 손대지 않음):
- 텍스처 생성기 `birch`(흰 자작 껍질, 검은 피목), `granite`(회색 화강암: 저대비 큰 톤 변화 + 가는 운모·석영 알갱이, 부드러운 지의류 반점 둘),
  `turf`(불투명 잔디·이끼), `leafcluster`(겹친 잎 알파 카드, `lobes=`로 참나무형 결각, `blossom=`으로 꽃), `flower`(줄기+꽃머리 카드). `mk.TEX_GENERATORS`에 런타임 등록.
- 나무는 재귀 가지(`grow`: 깊이별 길이·반지름·자식 수·위쪽 성향·흔들림, 밑동 `flare`, 자식이 나는 구간 `child_from`)에
  가지 끝마다 교차 잎 카드 다발(`leaf_cluster`)을 얹는다. 참나무 둘은 `leaf_min`으로 높이 30% 아래엔 잎을 두지 않아 줄기가 보인다.
- 바위 이끼: 면 노멀 y가 문턱(0.76~0.86)보다 큰 위쪽 면만 `moss` 머티리얼로 분리한다(머티리얼 스플릿이라 경계는 면 단위로 각진다).

## 파일과 PCG 슬롯

| 파일 | 슬롯 | 내용 | 높이 / 폭 | tris |
| --- | --- | --- | --- | --- |
| `SM_Grassland_Oak_A.glb` | tree | 참나무: 줄기 위 3/4까지 맨 줄기, 그 위 가지 4개·끝마다 잎 카드 5장 다발 | 3.80 m / 4.5 m | 4848 |
| `SM_Grassland_Birch_A.glb` | tree | 자작: 흰 껍질 곧은 줄기, 늘어진 작은 잎 카드 | 3.90 m / 2.9 m | 2432 |
| `SM_Grassland_Hawthorn_A.glb` | tree | 산사나무: 줄기 3개 떨기, 흰 꽃 섞인 잎 카드 | 2.70 m / 4.5 m | 1896 |
| `SM_Grassland_Stone_A.glb` | rock | fbm 화강암 바위(소), 이끼 없음 | 0.40 m / 0.7 m | 1656 |
| `SM_Grassland_Stone_B.glb` | rock | 화강암 바위(중), 윗면 이끼 | 0.55 m / 1.2 m | 1656 |
| `SM_Grassland_PebbleCluster_A.glb` | rock | 작은 돌 10개 무리, 셋에 하나 윗면 이끼 | 0.22 m / 1.0 m | 3520 |
| `SM_Grassland_MeadowGrass_A.glb` | grass | 풀 다발 5덩이(교차 블레이드 카드) | 0.85 m / 0.9 m | 120 |
| `SM_Grassland_Wildflower_A.glb` | grass | 풀 4덩이 + 데이지 6·수레국화 3(교차 카드) | 0.53 m / 0.6 m | 144 |
| `SM_Grassland_AncientOak_A.glb` | big | 고목 참나무: 굵고 벌어진 밑동, 높이 ~38%까지 맨 줄기, 굵은 가지 5개가 넓게 퍼진 둥근 수관 | 11.00 m / 11.7 m | 12950 |
| `SM_Grassland_GraniteTor_A.glb` | big | 화강암 토르: 둥근 판 4장 쌓음(윗면 이끼) + 기슭 바위 5개 | 6.20 m / 8.1 m | 9320 |
| `SM_Grassland_MeadowPatch_A.glb` | ground | 잔디 둔덕 원판(가장자리 y=0) + 클로버 카드 36·작은 꽃 40 | 0.14 m / 3.0 m | 2624 |

슬롯 규칙(키트 `SLOT_RULES`가 강제): tree 2.5–4 m(예산 8000), rock 0.15–1.5 m(8000), grass 0.2–1 m(8000),
big 높이 또는 폭 6–12 m(20000), ground 폭 2–4 m·높이 ≤0.15 m·y=0 위(6000). 세트당 tree 3 · rock 3 · grass 2 · big 2 · ground 1.
나무 높이는 슬롯 규칙에 맞춰 `fit_height`로 정확히 맞추고(가지 모양은 시드 랜덤), 바위는 0.2–0.55 m로 작게 뽑았다.

팔레트: 참나무 수피 (0.40, 0.30, 0.20), 자작 껍질 (0.90, 0.88, 0.82), 화강암 (0.56, 0.56, 0.53), 이끼 (0.21, 0.33, 0.14),
잔디 (0.33, 0.52, 0.17)/(0.60, 0.66, 0.24), 참나무 잎 (0.28, 0.47, 0.16)/(0.52, 0.62, 0.22), 수레국화 꽃잎 (0.45, 0.42, 0.82).

## 배치 권장값 (10000×10000 cm 존 기준)

- tree: Oak_A : Birch_A : Hawthorn_A = 4 : 4 : 2. 초원은 `tree_density` 0.1–0.25(빈 들판이 넓어야 초원답다). Hawthorn은 큰 나무 곁·가장자리에 모아 심는다.
- rock: Stone_A : Stone_B : PebbleCluster_A = 4 : 3 : 3. 모두 무릎 아래라 주 경로 가까이 둬도 된다.
- grass: MeadowGrass_A : Wildflower_A = 7 : 3. `grass_density`는 forest와 같거나 1.2배. Wildflower는 Oak·Tor 둘레에 비율을 올리면 보기 좋다.
- big(드물게): AncientOak_A는 존당 1–3그루, 주 경로(폭 390 cm) 마스크 밖 + 서로 2500 cm 이상 띄움(수관 폭 1170 cm).
  GraniteTor_A는 존당 1–2개, 역시 경로 마스크 밖; 기슭에 Stone·Pebble 밀도를 올린다.
- ground: MeadowPatch_A는 big·rock 주변과 빈 바닥에 밀도 0.3–0.6으로 깔고 서로 겹쳐도 된다(높이 14 cm). 콜리전 없음.

## UE 5.6 가져오기

1. Content Browser에서 `Content/Themes/Grassland/` 폴더를 만든다.
2. `meshes/*.glb`를 그 폴더로 드래그한다. Interchange glTF 임포터가 뜨면 Static Mesh로 가져온다
   (스케일 1.0 그대로; m→cm 변환은 임포터가 한다. 머티리얼·텍스처(baseColor, normal)는 glTF에서 그대로 들어오고,
   MASK 머티리얼은 Masked + Two Sided로 들어온다).
3. 콜리전: Oak·Birch·Hawthorn·AncientOak 줄기는 캡슐, Stone·PebbleCluster·GraniteTor는 단순 콜리전 자동 생성,
   MeadowGrass·Wildflower·MeadowPatch·잎 카드는 콜리전 없음.
4. PCG 그래프의 Static Mesh Spawner에서 슬롯별 메시 목록에 위 파일을 넣고 위 비율을 weight로 준다.

## 다시 만들기

`python3 generate_grassland_set.py` 한 번이면 11개 `.glb`와 미리보기 둘을 모두 다시 쓴다(시드 고정이라 결과는 같다).
모양을 바꾸려면 `OAK_PLAN`·`ANCIENT_OAK_PLAN` 등 가지 계획, `SPECS`의 잎 크기·`leaf_min`·`cards`, 바위의 `moss=` 문턱을 고친다.
실행 끝의 표(tris·H·W·bytes)와 `OK: 11 meshes verified`가 자체 검증 통과다. 키트 파일은 고치지 않는다.

## 새 테마 만들기

`../desert/README.md`의 "새 테마 만들기" 절과 같다. 키트에 없는 텍스처·형태가 필요하면 이 스크립트처럼
테마 파일 안에 생성기를 정의하고 `mk.TEX_GENERATORS`에 등록한다(키트 파일은 고치지 않는다).

## 알려진 공백 (클라이언트 담당자가 해야 할 일)

클라이언트에는 아직 **theme → 에셋 세트 전환이 없다.** `theme`은 `PCGZoneController.cpp`에서 파싱해 로그만 남긴다
(`[PCGZoneController] Parsed theme: ...`). PCG 그래프는 forest 세트(NorwayMaple, Megascans 바위, foliage)를 고정으로 쓴다.
따라서 이 메시들은 가져와도 게임에 자동으로 나오지 않는다. 테마별 메시 세트(예: theme 키로 찾는 DataAsset에
tree/rock/grass/big/ground 메시 배열을 두고, Zone 생성 시 PCG 그래프 파라미터로 넘기기)를 클라이언트에 추가해야 한다.
