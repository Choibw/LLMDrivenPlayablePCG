# Desert 에셋 세트 (테마별 PCG 에셋 템플릿)

`generate_desert_set.py` 한 파일이 사막 테마 메시 8개를 절차적으로 만들어 `meshes/*.glb`(glTF 2.0 바이너리)로 쓰고,
자체 검증 후 `preview.png`(컨택트 시트)를 그린다. 의존성은 Python 3 + numpy + matplotlib뿐이다(Blender 불필요).

```
python3 generate_desert_set.py
```

단위: 미터, glTF 규격대로 Y-up, 피벗은 바닥 중앙(y=0). 언리얼 Interchange 가져오기가 cm·Z-up으로 변환한다.
팔레트(sand / terracotta / sage / dry_wood / straw)는 스크립트 맨 위 `PALETTE` 한 곳에만 정의돼 있다.

## 파일과 PCG 슬롯

base-environment PCG 그래프는 `tree_density` / `rock_density` / `grass_density` 세 카테고리로 스캐터한다.

| 파일 | 슬롯 | 내용 | 높이 |
| --- | --- | --- | --- |
| `meshes/SM_Desert_Saguaro_A.glb` | tree | 팔 2개 사구아로 선인장 | 3.6 m |
| `meshes/SM_Desert_Saguaro_B.glb` | tree | 팔 3개 사구아로 선인장 | 2.8 m |
| `meshes/SM_Desert_DeadTree_A.glb` | tree | 잎 없는 뒤틀린 고사목 | 3.2 m |
| `meshes/SM_Desert_Rock_A.glb` | rock | 층리 사암 바위(소) | 0.9 m |
| `meshes/SM_Desert_Rock_B.glb` | rock | 층리 사암 바위(중) | 1.2 m |
| `meshes/SM_Desert_Mesa_Small.glb` | rock | 평평한 꼭대기 메사(드물게 배치) | 3.8 m |
| `meshes/SM_Desert_DryShrub_A.glb` | grass | 회전초 같은 마른 덤불 | 0.8 m |
| `meshes/SM_Desert_GrassTuft_A.glb` | grass | 두께 있는 마른 풀 다발 | 0.4 m |

모두 3000 삼각형 이하(최대 1950), 플랫 셰이딩, 프리미티브마다 PBR 머티리얼(baseColorFactor, roughness 0.9) 하나.

## 밀도 매핑 권장값 (10000×10000 cm 존 기준)

- tree 슬롯: Saguaro_A : Saguaro_B : DeadTree_A = 5 : 3 : 2. 사막은 `tree_density`가 낮게 오는 편(샘플 JSON 0.05)이라 그대로 두면 된다.
- rock 슬롯: Rock_A : Rock_B : Mesa_Small = 6 : 3 : 1. Mesa는 주 경로(폭 390 cm)와 겹치지 않게 경로 마스크 안쪽 배치 금지 + 최소 간격 1500 cm 권장.
- grass 슬롯: GrassTuft_A : DryShrub_A = 7 : 3. `grass_density`는 forest 세트보다 0.5배 정도로 희박하게 쓰는 편이 사막답다.

## UE 5.6 가져오기

1. Content Browser에서 `Content/Themes/Desert/` 폴더를 만든다.
2. `meshes/*.glb`를 그 폴더로 드래그한다. Interchange glTF 임포터가 뜨면 Static Mesh로 가져온다
   (스케일 1.0 그대로; m→cm 변환은 임포터가 한다. 머티리얼은 glTF PBR 색이 그대로 들어온다).
3. 콜리전: Saguaro·DeadTree·Rock·Mesa는 단순 콜리전 자동 생성 또는 캡슐, Grass·Shrub는 콜리전 없음.
4. PCG 그래프의 Static Mesh Spawner에서 슬롯별 메시 목록에 위 파일을 넣고 위 비율을 weight로 준다.

## grassland / snow 세트 만들기

1. `generate_desert_set.py`를 `../grassland/generate_grassland_set.py`처럼 복사한다.
2. `PALETTE` 값을 바꾼다(grassland: 초록·황토, snow: 흰색·청회색·어두운 침엽수색).
3. `SPECS` 목록의 이름·빌더·높이 범위를 바꾼다. 형태 함수(`saguaro`, `dead_tree`, `rock`, `mesa`, `dry_shrub`, `grass_tuft`)는
   `tube`(경로 스윕)·`uv_sphere`(변형 구) 두 프리미티브로만 만들어져 있으니, 예를 들어 침엽수는 `tube`로 줄기 + 원뿔 링,
   눈 덮인 바위는 `rock`에 윗면 밴드만 흰 머티리얼로 주는 식으로 재사용한다.
4. `write_glb` / `verify_glb` / `render_preview`는 건드리지 않는다. 실행해서 self-check와 preview.png를 확인한다.

## 알려진 공백 (클라이언트 담당자가 해야 할 일)

클라이언트에는 아직 **theme → 에셋 세트 전환이 없다.** `theme`은 `PCGZoneController.cpp`에서 파싱해 로그만 남긴다
(`[PCGZoneController] Parsed theme: ...`). PCG 그래프는 forest 세트(NorwayMaple, Megascans 바위, foliage)를 고정으로 쓴다.
따라서 이 메시들은 가져와도 게임에 자동으로 나오지 않는다. 테마별 메시 세트(예: theme 키로 찾는 DataAsset에
tree/rock/grass 메시 배열을 두고, Zone 생성 시 PCG 그래프 파라미터로 넘기기)를 클라이언트에 추가해야 한다.
