# Thực nghiệm C++17: FOCUS với ràng buộc Gaussian

## Chạy toàn bộ dữ liệu Facebook theo Phần 5

File `facebook` trong thư mục này là edge list SNAP đã được kiểm tra:

- 4.039 đỉnh, đánh số từ 0 đến 4038.
- 176.468 cung có hướng, tương ứng 88.234 cạnh vô hướng được ghi theo cả hai chiều.
- Dòng đầu chứa `4039 176468`; tiếp theo là 176.468 dòng cạnh `u v`.
- Phần `VERTEX_GAUSSIAN_COSTS_V1 4039` nằm sau các cạnh, tiếp theo là 4.039 dòng `vertex_id mu variance`. Chi phí ngẫu nhiên của đỉnh `e` tuân theo `N(mu[e], variance[e])`.

`experiment.cpp` dùng influence maximization theo mô hình Independent Cascade. Giá trị ảnh hưởng được ước lượng bằng một ngân hàng reverse-reachable (RR) cố định dùng chung cho FOCUS và Offline-Greedy-CC. Một ngân hàng RR độc lập được dùng để báo cáo `eval_f_value`, giúp phát hiện việc quá khớp với các mẫu dùng khi tối ưu.

Biên dịch và chạy từ thư mục `outputs`:

```bash
c++ -std=c++17 -O2 -Wall -Wextra -pedantic experiment.cpp -o experiment
./experiment --graph facebook --budget-ratio 0.01 --alpha 0.3 \
  --epsilon 0.02 --p 0.01 \
  --rr-samples 50000 --eval-samples 100000 --order random \
  > facebook_results.csv
```

Hoặc dùng `Makefile`:

```bash
make
make run
make run-one BUDGET_RATIO=0.01 ALPHA=0.3
```

`make run` chạy năm ngân sách bằng 1%, 2%, 3%, 4% và 5% tổng chi phí trung bình không nhiễu `sum(mu)`, rồi ghi ba tệp văn bản dạng TSV. Cả ba tệp có một dòng tiêu đề và đúng một dòng dữ liệu cho mỗi giá trị `B`. Có thể giảm số mẫu để chạy thử nhanh bằng `make run RR_SAMPLES=256 EVAL_SAMPLES=512 RESULT_DIR=results_smoke`.

- `results/facebook_results.txt` giữ định dạng hiện tại: `budget_ratio`, `B`, `sum_mu`, rồi đến `f_value`, `eval_f_value`, `queries`, `memory_mb_est` và `running_time_ms` của `Offline_Greedy_CC` và `FOCUS_RR`.
- `results/facebook_offline_metrics.txt` chỉ chứa kết quả của `Offline_Greedy_CC`, với các cột `%B`, `B`, `f_value`, `queries`, `memory_mb_est`, `running_time_ms`.
- `results/facebook_focus_metrics.txt` chỉ chứa kết quả của `FOCUS_RR`, với các cột `%B`, `B`, `f_value`, `queries`, `memory_mb_est`, `running_time_ms`.

Cột `%B` hiển thị tỷ lệ ngân sách dưới dạng phần trăm, chẳng hạn `1%`; cột `B` bên cạnh là giá trị ngân sách thực bằng tỷ lệ đó nhân với `sum_mu`.

Các cột được phân tách bằng tab nên có thể mở trực tiếp bằng trình soạn thảo văn bản hoặc nhập vào Excel.

Các giá trị tài nguyên đã được ghi cố định trong file `facebook`: `mu[e]` ban đầu được sinh độc lập trong khoảng `(0,1)` với seed 42 và `variance[e] = (0.5 * mu[e])^2`. Chương trình chỉ đọc `mu` và `variance`; nó không sinh lại chi phí khi chạy. Tùy chọn `--budget-ratio r` đặt `B = r * sum(mu)` và không phụ thuộc `alpha`. Không truyền đồng thời `--budget` và `--budget-ratio`.

Các seed chạy mặc định được tách riêng: RR tối ưu 43, RR đánh giá 44 và thứ tự luồng 45. Có thể thay bằng `--rr-seed`, `--eval-seed` và `--order-seed`. Dữ liệu chi phí không còn phụ thuộc tùy chọn dòng lệnh.

Xác suất IC mặc định `p=0.01` là một cấu hình thực nghiệm đề xuất vì PDF chưa công bố tham số IC. Tương tự, PDF chưa nêu cách sinh `mu`, `variance`, số mẫu hay số lần lặp; cần ghi rõ các lựa chọn này khi báo cáo.

Với RR coverage, hàm mục tiêu là đơn điệu. Vì vậy tập `S1` tự nó là nghiệm tối ưu của bài toán unconstrained trên các phần tử thuộc `S1`, và thay thế hợp lệ cho bước `BF-USM_1/2` mà không cần vét cạn. Chương trình lớn không tính `OPT`, vì việc vét cạn 4.039 đỉnh là bất khả thi.

Các cột chính được đặt ở đầu mỗi dòng CSV:

- `budget_ratio`, `B`, `sum_mu`: tỷ lệ ngân sách, ngân sách thực và tổng chi phí trung bình không nhiễu.
- `f_value`: giá trị hàm mục tiêu mà thuật toán tối ưu trên RR bank chung.
- `eval_f_value`: giá trị của nghiệm trên RR bank đánh giá độc lập.
- `queries`: số truy vấn oracle.
- `memory_mb_est`: RAM ước lượng gồm dữ liệu dùng chung và cấu trúc của thuật toán.
- `running_time_ms`: thời gian chạy riêng của thuật toán. Nó không gồm đọc graph và chi phí, tạo RR bank, sắp thứ tự đầu vào, đánh giá nghiệm độc lập hay ghi output.
- `feasible`, `chance_score`, `score_over_B`: kiểm tra ràng buộc Gaussian.
- `eval_f_se`, `eval_f_ci95_low`, `eval_f_ci95_high`: sai số chuẩn và khoảng tin cậy chuẩn xấp xỉ 95% trên RR bank đánh giá độc lập. Đây là khoảng có điều kiện cho một nghiệm đã chọn; nó không thay thế việc lặp qua nhiều resource/RR/order seed.
- `algorithm_memory_mb_est`, `shared_memory_mb_est`: tách phần nhớ thuật toán và dữ liệu graph/RR dùng chung.
- `rr_generation_ms`, `total_time_ms`: thời gian sinh hai RR bank và `running_time_ms + rr_generation_ms`. Tổng này không gồm đọc graph và chi phí, sắp thứ tự đầu vào, bước đánh giá cuối hay ghi CSV.
- `retained_active_states`, `retained_candidate_slots`: trạng thái FOCUS và tổng định danh còn được giữ khi thuật toán kết thúc.

Hai cột `retained_*` là chỉ số cấu trúc thuật toán, không phải tổng RAM. Việc tổng hợp các chỉ số và ước lượng bộ nhớ được thực hiện sau khi dừng đồng hồ, nên không bị tính vào `running_time_ms`. `memory_mb_est` là ước lượng bộ nhớ được giữ ở cuối thuật toán cùng dữ liệu dùng chung; nó không phải peak RSS của tiến trình.

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
