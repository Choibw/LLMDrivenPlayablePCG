# Desert 에셋 세트 (테마별 PCG 에셋 템플릿)

`generate_desert_set.py`가 공용 키트 `../common/meshkit.py` 위에서 사막 테마 메시 11개를 절차적으로 만들어
`meshes/*.glb`(glTF 2.0 바이너리, 텍스처 내장)로 쓰고, 자체 검증 후 `preview.webp`(컨택트 시트)와
`textures_preview.webp`(머티리얼별 baseColor·alpha 띠)를 그린다. 의존성은 Python 3 + numpy + matplotlib + Pillow뿐이다.

```
python3 generate_desert_set.py
```

단위: 미터, glTF 규격대로 Y-up, 피벗은 바닥 중앙(y=0). 언리얼 Interchange 가져오기가 cm·Z-up으로 변환한다.
미리보기는 `.png`가 아니라 `.webp`다(클라이언트 저장소가 `*.png`를 git-lfs로 추적하기 때문). 텍스처 PNG는 `.glb` 안에만 들어 있고 낱개 파일로는 없다.

품질 사양(키트 공통):
- 스무스 셰이딩: 꼭짓점 노멀 평균, 주름각 50° 초과(바위 층리·메사 턱·캡 가장자리)만 하드 엣지.
- 머티리얼마다 512² baseColor + 탄젠트 공간 노멀맵(높이장 → 중앙차분)을 BIN 청크에 내장. TEXCOORD_0 전 메시. TANGENT는 넣지 않음(언리얼이 MikkTSpace로 계산).
- 잎·풀·잔가지는 두꺼운 튜브 대신 알파 컷아웃 카드(alphaMode MASK, alphaCutoff 0.5, doubleSided). 교차·휜 배열이라 어느 각도에서도 읽힌다.

## 파일과 PCG 슬롯

| 파일 | 슬롯 | 내용 | 높이 / 폭 | tris |
| --- | --- | --- | --- | --- |
| `SM_Desert_Saguaro_A.glb` | tree | 팔 2개 사구아로(골 텍스처) | 3.60 m / 1.6 m | 1728 |
| `SM_Desert_Saguaro_B.glb` | tree | 팔 3개 사구아로 | 2.80 m / 1.5 m | 2272 |
| `SM_Desert_DeadTree_A.glb` | tree | 잎 없는 뒤틀린 고사목(나무결 텍스처) | 3.18 m / 1.1 m | 1296 |
| `SM_Desert_Rock_A.glb` | rock | 층리 사암 바위(소) | 0.35 m / 0.8 m | 720 |
| `SM_Desert_Rock_B.glb` | rock | 층리 사암 바위(중) | 0.60 m / 1.2 m | 720 |
| `SM_Desert_PebbleCluster_A.glb` | rock | 작은 돌 7개 무리 | 0.15 m / 0.8 m | 1764 |
| `SM_Desert_DryShrub_A.glb` | grass | 마른 덤불(잔가지 카드 14장 + 밑동) | 0.65 m / 1.3 m | 136 |
| `SM_Desert_GrassTuft_A.glb` | grass | 마른 풀 다발(교차 카드 3덩이) | 0.46 m / 0.6 m | 36 |
| `SM_Desert_Mesa_Large.glb` | big | 층리 사암 메사 + 기슭 낙석 5개 | 7.00 m / 9.6 m | 3612 |
| `SM_Desert_DatePalm_A.glb` | big | 대추야자: 휜 고리 줄기 + 늘어진 깃꼴 잎 13장(교차 카드) | 9.45 m / 7.8 m | 1468 |
| `SM_Desert_SandRipple_A.glb` | ground | 낮은 모래 물결 원판(가장자리 y=0) | 0.05 m / 3.0 m | 2256 |

슬롯 규칙(키트 `SLOT_RULES`가 강제): tree 2.5–4 m(예산 8000), rock 0.15–1.5 m(8000), grass 0.2–1 m(8000),
big 높이 또는 폭 6–12 m(20000), ground 폭 2–4 m·높이 ≤0.15 m·y=0 위(6000). 세트당 tree 3 · rock 3 · grass 2 · big 2 · ground 1.

## 배치 권장값 (10000×10000 cm 존 기준)

- tree: Saguaro_A : Saguaro_B : DeadTree_A = 5 : 3 : 2. 사막은 `tree_density`가 낮게 오는 편(샘플 JSON 0.05).
- rock: Rock_A : Rock_B : PebbleCluster_A = 4 : 3 : 3. 모두 사람 허리 아래라 주 경로 가까이 둬도 된다.
- grass: GrassTuft_A : DryShrub_A = 7 : 3. `grass_density`는 forest 세트의 0.5배 정도가 사막답다.
- big(드물게, 존당 몇 개): Mesa_Large는 존당 1–2개, 주 경로(폭 390 cm) 마스크 밖 + 서로 최소 2500 cm 간격.
  DatePalm_A는 존당 3–6개를 2–4그루씩 모아 "오아시스"처럼; 역시 경로 마스크 밖.
- ground: SandRipple_A는 big·rock 주변과 빈 바닥에 밀도 0.3–0.6으로 깔고, 서로 겹쳐도 된다(높이 5 cm). 콜리전 없음.

## UE 5.6 가져오기

1. Content Browser에서 `Content/Themes/Desert/` 폴더를 만든다.
2. `meshes/*.glb`를 그 폴더로 드래그한다. Interchange glTF 임포터가 뜨면 Static Mesh로 가져온다
   (스케일 1.0 그대로; m→cm 변환은 임포터가 한다. 머티리얼·텍스처(baseColor, normal)는 glTF에서 그대로 들어오고,
   MASK 머티리얼은 Masked + Two Sided로 들어온다).
3. 콜리전: Saguaro·DeadTree·Rock·Mesa·DatePalm 줄기는 단순 콜리전 자동 생성 또는 캡슐, Grass·Shrub·SandRipple·야자 잎은 콜리전 없음.
4. PCG 그래프의 Static Mesh Spawner에서 슬롯별 메시 목록에 위 파일을 넣고 위 비율을 weight로 준다.

## 새 테마 만들기 (grassland / snow / …)

1. `../<theme>/generate_<theme>_set.py`를 만든다. 내용은 `PALETTE` + 형태 함수 + `SPECS` + `mk.run(...)`뿐이다
   (`generate_desert_set.py`를 복사해서 고치면 된다). 키트는 `sys.path`로 `../common`을 넣고 `import meshkit as mk`.
2. `PALETTE`: 키마다 `{"color": (r,g,b), "tex": <생성기>, ...옵션}`. 생성기는 `mk.TEX_GENERATORS`:
   `bark`(rings=), `rock`(color2=, strata=), `cactus`(ribs=), `sand`, `frond`, `blade`, `twig`(마지막 셋은 알파 카드용 → 자동 MASK).
3. 형태 함수는 `mk.tube`(경로 스윕, 골·mod), `mk.uv_sphere`, `mk.column`, `mk.rock_blob`(fbm 변형·층리 바위),
   `mk.heightfield_disc`(바닥 패치), `mk.ribbon`(카드), `mk.place`(스케일·Y회전·이동)로 만들고 `Mesh.add(V, F, UV, 팔레트키)`로 모은다.
4. `SPECS = [mk.Spec(이름, 슬롯, lambda seed: 빌더(seed, ...), seed, tri예산), ...]` 11개, 위 슬롯 규칙대로.
5. `mk.run("Theme", PALETTE, SPECS, Path(__file__).resolve().parent)` 실행 → 슬롯 수·높이·폭·tris 예산·GLB 구조·텍스처 내장을
   assert로 검사하고 `preview.webp`를 쓴다. 실패하면 메시지대로 크기나 예산을 고친다.

## 알려진 공백 (클라이언트 담당자가 해야 할 일)

클라이언트에는 아직 **theme → 에셋 세트 전환이 없다.** `theme`은 `PCGZoneController.cpp`에서 파싱해 로그만 남긴다
(`[PCGZoneController] Parsed theme: ...`). PCG 그래프는 forest 세트(NorwayMaple, Megascans 바위, foliage)를 고정으로 쓴다.
따라서 이 메시들은 가져와도 게임에 자동으로 나오지 않는다. 테마별 메시 세트(예: theme 키로 찾는 DataAsset에
tree/rock/grass/big/ground 메시 배열을 두고, Zone 생성 시 PCG 그래프 파라미터로 넘기기)를 클라이언트에 추가해야 한다.
