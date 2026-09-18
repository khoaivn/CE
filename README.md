# Thực nghiệm C++17: Gaussian chance-constrained maximum cut

Mã độc lập, chỉ dùng thư viện chuẩn C++17. Đây là bộ kiểm chứng trên dữ liệu nhỏ (1–20 đỉnh), không phải bản triển khai quy mô lớn với độ phức tạp của bài báo.

## Biên dịch và chạy

```bash
c++ -std=c++17 -O2 -Wall -Wextra -pedantic experiment.cpp -o experiment
./experiment > results.csv
```

Tham số theo thứ tự:

```text
./experiment n seed epsilon alpha budget_ratio uncertainty order
./experiment 18 42 0.02 0.05 0.2 0.5 random > results.csv
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
