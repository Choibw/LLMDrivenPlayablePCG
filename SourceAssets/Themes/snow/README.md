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
- 눈 쌓임: 바위는 꼭짓점마다 점수 = 구 노멀 y + 노이즈를 매기고 세 꼭짓점 평균이 문턱을 넘는 면만 `snow` 머티리얼로 분리한다
  (위도 링을 따라 계단지던 경계가 불규칙해짐). 눈 꼭짓점은 눈 두께만큼 올리고 경계 꼭짓점은 바깥으로 밀어 얇은 처마를 만든다. 빙탑 본체는 면 노멀 y>0.5 스플릿 그대로.
  침엽·향나무는 가지 카드 위에 `snowcap` 카드를 2.5~5 cm 띄워 얹는다.
- `M_Snow_ice`는 생성 뒤 GLB JSON을 다시 써서 roughness 0.18(키트는 전 머티리얼 0.9 고정). 투과(transmission)는 넣지 않았다 — 연한 파랑·저러프니스뿐이다.

## 파일과 PCG 슬롯

| 파일 | 슬롯 | 내용 | 높이 / 폭 | tris |
| --- | --- | --- | --- | --- |
| `SM_Snow_Spruce_A.glb` | tree | 눈 쌓인 가문비: 12단 늘어진 침엽 카드 가지 + 위 눈 베개(폭 0.72) | 3.85 m / 2.35 m | 3234 |
| `SM_Snow_Spruce_B.glb` | tree | 작은 가문비(10단, 더 늘어짐) | 3.00 m / 2.16 m | 2454 |
| `SM_Snow_Birch_A.glb` | tree | 서리 낀 자작, 꽉 찬 수관: 4단 갈래 가지 끝마다 서리 잔가지 카드 4장 교차 + 눈 덩이 카드 | 3.90 m / 2.10 m | 5348 |
| `SM_Snow_Rock_A.glb` | rock | fbm 점판암 바위(소), 눈 모자(노멀+노이즈 경계, 얇은 처마) | 0.48 m / 0.92 m | 1344 |
| `SM_Snow_Rock_B.glb` | rock | 점판암 바위(중), 두꺼운 눈 모자 | 0.69 m / 1.54 m | 1344 |
| `SM_Snow_RockCluster_A.glb` | rock | 넓고 낮은 눈 둔덕(fbm 윤곽, 가장자리 기울기 0) + 돌 끝 4개 | 0.34 m / 2.35 m | 2948 |
| `SM_Snow_FrozenGrass_A.glb` | grass | 마른 풀 10덩이(교차 카드, 가운데가 큼) + 줄기 밑 작은 눈 덩이 3개. 바닥 원판 없음 | 0.46 m / 1.06 m | 624 |
| `SM_Snow_Juniper_A.glb` | grass | 빽빽한 낮은 향나무: 가지 30개 3겹 + 가지마다 눈 베개 + 꼭대기 눈 덩이 4개. 바닥 원판 없음 | 0.57 m / 1.25 m | 2020 |
| `SM_Snow_GiantFir_A.glb` | big | 거대 전나무 20단(가지 11개/단) | 11.50 m / 4.96 m | 7638 |
| `SM_Snow_IceSpire_A.glb` | big | 빙탑: 위로 좁아지는 fbm 빙하 덩어리(얼음+상부 눈) + 기슭 점판암 5개 | 6.60 m / 3.85 m | 6968 |
| `SM_Snow_SnowDrift_A.glb` | ground | 바람 깎은 눈 더미: fbm 윤곽, 가장자리에서 높이·기울기 0, 완만한 사스트루기 | 0.11 m / 5.77 m | 3776 |

슬롯 규칙(키트 `SLOT_RULES`가 강제): tree 2.5–4 m(예산 8000), rock 0.15–1.5 m(8000), grass 0.2–1 m(8000),
big 높이 또는 폭 6–12 m(20000), ground 폭 2–4 m·높이 ≤0.15 m·y=0 위(6000).
엔진 검토(2차)에서 SnowDrift를 5–6 m로 키우라고 해서 이 스크립트가 실행 시 `mk.SLOT_RULES["ground"]` 폭 상한만 6.5 m로 덮어쓴다(키트 파일은 그대로). 세트당 tree 3 · rock 3 · grass 2 · big 2 · ground 1.
요청 사양의 tree 2.5–5 m는 키트 상한 4 m에 맞췄고, 바위는 "사막 바위가 컸다"는 피드백(0.9 m 넘으면 큼)과 "설원이 비어 보인다"는 2차 검토 사이에서 0.48 / 0.69 m로 잡았다.

팔레트: 눈 (0.90, 0.93, 0.99)·그늘 (0.60, 0.70, 0.88), 가문비 침엽 (0.11, 0.24, 0.14), 수피 (0.36, 0.29, 0.24),
점판암 (0.31, 0.33, 0.37)/(0.22, 0.24, 0.27), 얼음 (0.70, 0.84, 0.96).

## 배치 권장값 (10000×10000 cm 존 기준)

- **설원 PCG 밀도 제안: tree 0.3–0.4, rock 0.35, grass 0.2.** 그리고 **랜드스케이프 자체에 눈 머티리얼을 입혀야 한다** —
  `SM_Snow_SnowDrift_A`의 `M_Snow_snow` baseColor+normal(512², 이음새 없이 타일됨; 스크립트가 시작할 때 이음새를 검사한다)을 랜드스케이프 머티리얼에
  그대로 써도 된다. 회색 맨 바닥이 맵이 비어 보이는 가장 큰 이유다.

- tree: Spruce_A : Spruce_B : Birch_A = 5 : 3 : 2. 설원은 `tree_density` 0.1–0.2가 어울린다.
- rock: Rock_A : Rock_B : RockCluster_A = 4 : 3 : 3. 모두 무릎 아래라 주 경로 가까이 둬도 된다. RockCluster_A는 이제 2.35 m 눈 둔덕이라 빈 땅 메우기용.
- grass: FrozenGrass_A : Juniper_A = 6 : 4. 바닥 원판을 뺐으니 촘촘히 뿌려도 물방울 무늬가 생기지 않는다.
- big(드물게): GiantFir_A는 존당 2–4그루, 서로 1500 cm 이상 띄움. IceSpire_A는 존당 1–2개, 주 경로(폭 390 cm) 마스크 밖.
- ground: SnowDrift_A(폭 5.8 m)는 빈 바닥과 big·rock 주변에 밀도 0.2–0.4로 깔고 서로 겹쳐도 된다(높이 11 cm, 가장자리 기울기 0이라 지형과 섞인다). 콜리전 없음.
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
