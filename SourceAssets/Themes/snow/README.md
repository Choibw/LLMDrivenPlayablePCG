# Snow 에셋 세트 (테마별 PCG 에셋)

`generate_snow_set.py`가 공용 키트 `../common/meshkit.py` 위에서 설원 테마 메시 11개를 절차적으로 만들어
`meshes/*.glb`(glTF 2.0 바이너리, 텍스처 내장)로 쓰고, 자체 검증 후 `preview.webp`(컨택트 시트)와
`textures_preview.webp`(머티리얼별 baseColor·alpha 띠)를 그린다. 의존성은 Python 3 + numpy + matplotlib + Pillow뿐이다.

```
python3 generate_snow_set.py
```

단위: 미터, glTF 규격대로 Y-up, 피벗은 바닥 중앙(y=0). 언리얼 Interchange 가져오기가 cm·Z-up으로 변환한다.
미리보기는 `.png`가 아니라 `.webp`다(클라이언트 저장소가 `*.png`를 git-lfs로 추적하기 때문). 텍스처 PNG는 `.glb` 안에만 들어 있고 낱개 파일로는 없다.

품질 사양(키트 공통):
- 스무스 셰이딩: 꼭짓점 노멀 평균, 주름각 50° 초과(바위 능선·빙하 층·캡 가장자리)만 하드 엣지.
- 머티리얼마다 512² baseColor + 탄젠트 공간 노멀맵(높이장 → 중앙차분)을 BIN 청크에 내장. TEXCOORD_0 전 메시. TANGENT는 넣지 않음(언리얼이 MikkTSpace로 계산).
- 침엽·풀·잔가지·눈 베개는 두꺼운 튜브 대신 알파 컷아웃 카드(alphaMode MASK, alphaCutoff 0.5, doubleSided). 교차·휜 배열이라 어느 각도에서도 읽힌다.

이 테마만의 추가(스크립트 안에 정의, 키트는 손대지 않음):
- 텍스처 생성기 `snow`(청백 눈, 그늘은 푸르게·간간이 반짝임), `birch`(자작 껍질, 검은 가로 피목), `ice`(빙하 얼음, 균열·층 띠),
  `needle`(침엽 가지 알파 카드, `frost=`로 끝 서리), `snowcap`(가지 위 눈 베개 알파 카드). `mk.TEX_GENERATORS`에 런타임 등록.
- 눈 쌓임: 바위·빙탑은 면 노멀 y>0.4 이고 상반부인 면만 `snow` 머티리얼로 분리(머티리얼 스플릿)하고, 위를 향한 꼭짓점을 눈 두께만큼 들어올린다.
  침엽·향나무는 가지 카드 위에 `snowcap` 카드를 2.5~5 cm 띄워 얹는다.
- `M_Snow_ice`는 생성 뒤 GLB JSON을 다시 써서 roughness 0.18(키트는 전 머티리얼 0.9 고정). 투과(transmission)는 넣지 않았다 — 연한 파랑·저러프니스뿐이다.

## 파일과 PCG 슬롯

| 파일 | 슬롯 | 내용 | 높이 / 폭 | tris |
| --- | --- | --- | --- | --- |
| `SM_Snow_Spruce_A.glb` | tree | 눈 쌓인 가문비: 12단 늘어진 침엽 카드 가지 + 위 눈 베개 | 3.85 m / 2.4 m | 3234 |
| `SM_Snow_Spruce_B.glb` | tree | 작은 가문비(10단, 더 늘어짐) | 3.00 m / 2.2 m | 2454 |
| `SM_Snow_Birch_A.glb` | tree | 서리 낀 맨 자작: 흰 껍질 줄기·갈래 가지 + 끝 잔가지 카드 | 3.90 m / 1.4 m | 2484 |
| `SM_Snow_Rock_A.glb` | rock | fbm 점판암 바위(소), 윗면 눈 | 0.34 m / 0.7 m | 1344 |
| `SM_Snow_Rock_B.glb` | rock | 점판암 바위(중), 윗면 눈 | 0.42 m / 1.0 m | 1344 |
| `SM_Snow_RockCluster_A.glb` | rock | 작은 돌 4개 무리, 각각 눈 모자 | 0.23 m / 0.7 m | 2080 |
| `SM_Snow_FrozenGrass_A.glb` | grass | 눈 둔덕을 뚫고 나온 마른 풀 4덩이(교차 카드) | 0.45 m / 0.7 m | 246 |
| `SM_Snow_Juniper_A.glb` | grass | 낮은 향나무: 줄기 9개 + 서리 침엽 카드 + 눈 베개 + 눈 둔덕 | 0.48 m / 1.0 m | 622 |
| `SM_Snow_GiantFir_A.glb` | big | 거대 전나무 20단(가지 11개/단) | 11.50 m / 5.0 m | 7638 |
| `SM_Snow_IceSpire_A.glb` | big | 빙탑: 위로 좁아지는 fbm 빙하 덩어리(얼음+상부 눈) + 기슭 점판암 5개 | 6.60 m / 3.8 m | 6968 |
| `SM_Snow_SnowDrift_A.glb` | ground | 바람 깎은 눈 더미, 사스트루기 능선(가장자리 y=0) | 0.11 m / 3.2 m | 2754 |

슬롯 규칙(키트 `SLOT_RULES`가 강제): tree 2.5–4 m(예산 8000), rock 0.15–1.5 m(8000), grass 0.2–1 m(8000),
big 높이 또는 폭 6–12 m(20000), ground 폭 2–4 m·높이 ≤0.15 m·y=0 위(6000). 세트당 tree 3 · rock 3 · grass 2 · big 2 · ground 1.
요청 사양의 tree 2.5–5 m는 키트 상한 4 m에 맞췄고, 바위는 "사막 바위가 컸다"는 피드백대로 0.2–0.5 m로 작게 뽑았다.

팔레트: 눈 (0.90, 0.93, 0.99)·그늘 (0.60, 0.70, 0.88), 가문비 침엽 (0.11, 0.24, 0.14), 수피 (0.36, 0.29, 0.24),
점판암 (0.31, 0.33, 0.37)/(0.22, 0.24, 0.27), 얼음 (0.70, 0.84, 0.96).

## 배치 권장값 (10000×10000 cm 존 기준)

- tree: Spruce_A : Spruce_B : Birch_A = 5 : 3 : 2. 설원은 `tree_density` 0.1–0.2가 어울린다.
- rock: Rock_A : Rock_B : RockCluster_A = 4 : 3 : 3. 모두 무릎 아래라 주 경로 가까이 둬도 된다.
- grass: FrozenGrass_A : Juniper_A = 6 : 4. `grass_density`는 forest의 0.4배 정도(눈밭은 비어 보이는 게 자연스럽다).
- big(드물게): GiantFir_A는 존당 2–4그루, 서로 1500 cm 이상 띄움. IceSpire_A는 존당 1–2개, 주 경로(폭 390 cm) 마스크 밖.
- ground: SnowDrift_A는 빈 바닥과 big·rock 주변에 밀도 0.4–0.7로 깔고 서로 겹쳐도 된다(높이 11 cm). 콜리전 없음.
  바람 방향을 맞추려면 Y 회전을 존별 한 값(±15°)으로 고정한다(사스트루기 능선이 x축을 가로지른다).

## UE 5.6 가져오기

1. Content Browser에서 `Content/Themes/Snow/` 폴더를 만든다.
2. `meshes/*.glb`를 그 폴더로 드래그한다. Interchange glTF 임포터가 뜨면 Static Mesh로 가져온다
   (스케일 1.0 그대로; m→cm 변환은 임포터가 한다. 머티리얼·텍스처(baseColor, normal)는 glTF에서 그대로 들어오고,
   MASK 머티리얼은 Masked + Two Sided로 들어온다. `M_Snow_ice`는 roughness 0.18로 들어온다).
3. 콜리전: Spruce·Birch·GiantFir 줄기는 캡슐, Rock·IceSpire는 단순 콜리전 자동 생성, Grass·Juniper·SnowDrift·침엽 카드는 콜리전 없음.
4. PCG 그래프의 Static Mesh Spawner에서 슬롯별 메시 목록에 위 파일을 넣고 위 비율을 weight로 준다.

## 새 테마 만들기

`../desert/README.md`의 "새 테마 만들기" 절과 같다. 키트에 없는 텍스처·형태가 필요하면 이 스크립트처럼
테마 파일 안에 생성기를 정의하고 `mk.TEX_GENERATORS`에 등록한다(키트 파일은 고치지 않는다).

## 알려진 공백 (클라이언트 담당자가 해야 할 일)

클라이언트에는 아직 **theme → 에셋 세트 전환이 없다.** `theme`은 `PCGZoneController.cpp`에서 파싱해 로그만 남긴다
(`[PCGZoneController] Parsed theme: ...`). PCG 그래프는 forest 세트(NorwayMaple, Megascans 바위, foliage)를 고정으로 쓴다.
따라서 이 메시들은 가져와도 게임에 자동으로 나오지 않는다. 테마별 메시 세트(예: theme 키로 찾는 DataAsset에
tree/rock/grass/big/ground 메시 배열을 두고, Zone 생성 시 PCG 그래프 파라미터로 넘기기)를 클라이언트에 추가해야 한다.
