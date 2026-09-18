# Thực nghiệm C++17: FOCUS với ràng buộc Gaussian

## Chạy toàn bộ dữ liệu Facebook theo Phần 5

File `facebook` trong thư mục này là edge list SNAP đã được kiểm tra:

- 4.039 đỉnh, đánh số từ 0 đến 4038.
- 176.468 cung có hướng, tương ứng 88.234 cạnh vô hướng được ghi theo cả hai chiều.
- Dòng đầu chứa `4039 176468`; các dòng sau chứa `u v`.

`experiment.cpp` dùng influence maximization theo mô hình Independent Cascade. Giá trị ảnh hưởng được ước lượng bằng một ngân hàng reverse-reachable (RR) cố định dùng chung cho FOCUS và Offline-Greedy-CC. Một ngân hàng RR độc lập được dùng để báo cáo `eval_spread`, giúp phát hiện việc quá khớp với các mẫu dùng khi tối ưu.

Biên dịch và chạy từ thư mục `outputs`:

```bash
c++ -std=c++17 -O2 -Wall -Wextra -pedantic experiment.cpp -o experiment
./experiment --graph facebook --budget 100 --alpha 0.05 \
  --epsilon 0.02 --uncertainty 0.5 --p 0.01 \
  --rr-samples 50000 --eval-samples 100000 --order random \
  > facebook_results.csv
```

Ngân sách `--budget` là giá trị tuyệt đối và không tự thay đổi khi quét `alpha`. Có thể dùng `--budget-ratio r` để đặt `B = r * sum(mu)`; cách này cũng độc lập với `alpha`. Không truyền đồng thời hai tùy chọn ngân sách.

Các seed mặc định được tách riêng: tài nguyên 42, RR tối ưu 43, RR đánh giá 44 và thứ tự luồng 45. Có thể thay bằng `--resource-seed`, `--rr-seed`, `--eval-seed` và `--order-seed`. Giữ nguyên resource seed khi so sánh các giá trị `B`, `alpha` hoặc `epsilon`.

Xác suất IC mặc định `p=0.01` là một cấu hình thực nghiệm đề xuất vì PDF chưa công bố tham số IC. Tương tự, PDF chưa nêu cách sinh `mu`, `variance`, số mẫu hay số lần lặp. Chương trình sinh `mu` đều trong `[1,10)` và đặt `sigma = uncertainty * mu`; cần ghi rõ các lựa chọn này khi báo cáo.

Với RR coverage, hàm mục tiêu là đơn điệu. Vì vậy tập `S1` tự nó là nghiệm tối ưu của bài toán unconstrained trên các phần tử thuộc `S1`, và thay thế hợp lệ cho bước `BF-USM_1/2` mà không cần vét cạn. Chương trình lớn không tính `OPT`, vì việc vét cạn 4.039 đỉnh là bất khả thi.

Các cột quan trọng:

- `train_spread`: ảnh hưởng trên RR bank dùng bởi thuật toán.
- `eval_spread`: ảnh hưởng trên RR bank độc lập, nên dùng làm số liệu chất lượng chính.
- `eval_se`, `eval_ci95_low`, `eval_ci95_high`: sai số chuẩn và khoảng tin cậy chuẩn xấp xỉ 95% trên RR bank đánh giá độc lập. Đây là khoảng có điều kiện cho một nghiệm đã chọn; nó không thay thế việc lặp qua nhiều resource/RR/order seed.
- `chance_score`, `score_over_B`, `feasible`: kiểm tra ràng buộc Gaussian.
- `queries`, `algorithm_ms`: số truy vấn oracle và thời gian của riêng thuật toán.
- `rr_generation_ms`, `total_ms`: thời gian sinh hai RR bank và `algorithm_ms + rr_generation_ms`. `total_ms` không gồm đọc graph, sinh tài nguyên, bước đánh giá cuối hay ghi CSV, nên không phải wall-clock end-to-end.
- `peak_active_states`, `peak_candidate_slots`: trạng thái FOCUS đang hoạt động và tổng định danh được giữ trong hai tập ứng viên.

Hai cột `peak_*` là chỉ số cấu trúc thuật toán, không phải tổng RAM: chúng chưa tính terminal registry, bitset RR của từng candidate, hai RR bank và metadata của container. Khi báo cáo retained memory theo Phần 5, cần đo thêm peak RSS của tiến trình hoặc bổ sung bộ đếm byte.

Chương trình giữ cách hiểu theo phần diễn giải của tài liệu rằng trạng thái đi qua nhánh big-item trở thành terminal vĩnh viễn. Registry terminal không được tính là trạng thái active.

`facebook_sample_results.csv` là kết quả cấu hình mặc định. Danh sách seed được in ra stderr để stdout luôn là CSV sạch.

---

## Bộ kiểm chứng nhỏ được giữ lại

`experiment_small.cpp` là mã max-cut trước đây, dùng để đối chiếu OPT trên dữ liệu nhỏ 1–20 đỉnh. `facebook_experiment.cpp` được giữ làm tên tương thích và chỉ nạp `experiment.cpp`.

## Biên dịch và chạy

```bash
c++ -std=c++17 -O2 -Wall -Wextra -pedantic experiment_small.cpp -o experiment_small
./experiment_small > results.csv
```

Tham số theo thứ tự:

```text
./experiment_small n seed epsilon alpha budget_ratio uncertainty order
./experiment_small 18 42 0.02 0.05 0.2 0.5 random > results.csv
```

Mặc định: `16 42 0.02 0.05 0.2 0.5 random`.

- `n`: số đỉnh, từ 1 đến 20.
- `seed`: seed sinh tài nguyên, đồ thị và thứ tự luồng.
- `epsilon`: trong khoảng `(0, 1/16)`.
- `alpha`: trong khoảng `[1e-12, 0.5)`.
- `budget_ratio`: trong khoảng `(0,1]`; ngân sách B = budget_ratio × g(V).
- `uncertainty`: trong khoảng `(0,100]`; sigma[e] = uncertainty × mu[e].
- `order`: `random`, `mu_asc` hoặc `mu_desc`.

Mỗi cặp đỉnh có cạnh với xác suất 0.3; trọng số cạnh nguyên trong [1,10]. Trung bình tài nguyên lấy đều trong [1,10). Các tài nguyên được mô hình hóa độc lập với phương sai dương. Hàm mục tiêu là tổng trọng số các cạnh có đúng một đầu thuộc tập được chọn. Hàm g(S) = tổng mu + z_alpha × căn(tổng variance). Không cần mô phỏng Gaussian để kiểm tra ràng buộc.

## Ba phương pháp

1. `OPT`: vét cạn tất cả tập khả thi.
2. `Offline_Greedy_CC`: tham lam theo lợi ích biên / chi phí biên, dừng khi không còn lợi ích dương; so sánh với phần tử đơn khả thi tốt nhất.
3. `FOCUS_EXACT_USM`: cấu trúc luồng của Algorithm 1, dùng vét cạn tìm tập con tốt nhất của S1 thay cho BF-USM. Kết quả hậu xử lý được ghi nhớ cho các S1 trùng nhau.

FOCUS dùng q=3/2, delta=16×epsilon, lưới giá trị (1+delta)^k với k nguyên, kích hoạt trước khi xử lý phần tử vượt ngưỡng, hai tập ứng viên, nhánh phần tử lớn và nghiệm đơn tốt nhất. Trạng thái kết thúc được giữ làm dấu để tránh tái kích hoạt cùng cặp (j,k); có thể bị xóa khi mức đoán đã quá thấp. Các trạng thái có giá trị singleton cực đại bằng 0 không sinh mức đoán.

**Khác biệt cần nêu khi báo cáo:** hậu xử lý vét cạn cho nghiệm tối ưu trên S1, nhưng có chi phí mũ. Không được dùng thời gian hay bộ nhớ của bản này để khẳng định độ phức tạp của FOCUS với BF-USM trong bài báo. Mã dùng long double, không phải số học thực chính xác. Kiểm tra khả thi bổ sung theo g(S) trước khi thêm phần tử giúp hạn chế sai số tích lũy; không phải chứng nhận số học chính xác tại biên.

## Đọc kết quả

CSV có giá trị mục tiêu, tỷ lệ với OPT, g(S), g(S)/B, tính khả thi, kích thước tập, số gọi oracle, thời gian và thống kê trạng thái. Khi OPT=0, tỷ lệ là `NA`. Danh sách đỉnh được chọn (đánh số từ 0) in ra stderr nên không lẫn vào CSV.

- `queries`: số lời gọi hàm f thực sự trong từng phương pháp, gồm hậu xử lý; cache có thể giảm số gọi. Thứ tự kiểm tra trong mã ưu tiên kiểm tra chi phí trước khi gọi oracle.
- `ms`: thời gian phương pháp, không gồm sinh dữ liệu và tiền tính oracle.
- `peak_states`: số trạng thái lưu cực đại, bao gồm dấu trạng thái kết thúc còn lưu.
- `peak_candidate_slots`: tổng số định danh phần tử trong hai tập ứng viên qua các trạng thái, có tính lặp. Không phải số byte RAM, không gồm incumbent, metadata hay cache hậu xử lý.
- `tangents`: số tiếp tuyến được tạo.

Oracle cắt đồ thị được tiền tính cho mọi tập bằng bảng O(2^n), vì mục đích kiểm chứng nhỏ. Bảng này thuộc bộ chạy thực nghiệm, không phải bộ nhớ luồng của FOCUS. Hàm `focus` chỉ nhận phần tử theo thứ tự luồng; dùng bitmask và n để biểu diễn tập. Muốn chạy lớn phải thay oracle này, cấu trúc tập và thủ tục hậu xử lý.

## Kiểm tra

Chương trình tự báo lỗi nếu nghiệm không khả thi hoặc FOCUS_EXACT_USM vi phạm mốc (1/16-epsilon)×OPT (dùng dung sai 1e-10 khi so giá trị). Thử nghiệm hữu hạn không chứng minh định lý.

Đã biên dịch với cảnh báo bật và chạy kiểm tra 90 cấu hình: n=12, seed=0..9, ba thứ tự luồng, ba tỷ lệ ngân sách 0.01/0.2/1. Tất cả nghiệm khả thi, giá trị FOCUS không vượt OPT và vượt qua kiểm tra mốc xấp xỉ. `sample_results.csv` là lần chạy cấu hình mặc định.

Để dùng dữ liệu riêng, thay hàm `generate`: điền ma trận trọng số đối xứng không âm, mu, variance, B, z và bảng oracle; vẫn giữ n<=20 cho bộ kiểm chứng này.
