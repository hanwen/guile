(begin
  ;; Force GC
  (gc)
(display "gc2: \n")
  ;; Force GC to sweep the heap, so cells-allocated is correct.
  (gc)
  (display "gc2 done\n")
  (if  
   (let*
       ((start-stats (gc-stats))
	(xx (begin (display start-stats) (newline)))
	(total-cells-allocated (cdr (assoc 'total-cells-allocated start-stats)))

	(live-data-size (cdr (assoc 'cells-allocated start-stats)))

	;; Calculate heap size, assuming 40% default yield and 10% extra fuzz
	(ideal-heap-size (inexact->exact (truncate (* 1.1 (truncate (/ live-data-size 0.60))))))

	(frac 16)
	
	;; Cycle through an entire heap 10 times.
	(count 10)

	;; Alloc in chunks of this size. Use a fraction of the heap size,
	;; because this chunk of memory will be reachable.
	(increment (quotient ideal-heap-size frac))

	(tried-total-alloc (* frac count increment))

    	(ok #t)
	(unused (map 
		 (lambda (unused)
		   ;; Use make-list which is a scheme primitive with precise memory
		   ;; requirements.
		   (make-list increment #f)
		   (let*
		       ((st (gc-stats)))
;		     (display st)(newline)
		     (if (> (cdr (assoc 'cells-allocated st)) (cdr (assoc 'cell-heap-size st)))
			 (begin
			   (display (format #f "cells-allocated > cell-heap-size: ~a\n" st))
			   (set! ok #f)))
		     
		     1))

		 (iota (* frac count))))

	(end-stats (gc-stats))
	(after-heap-size (cdr (assoc 'cell-heap-size  end-stats)))
	)

     (if (> after-heap-size ideal-heap-size)
	 (begin
	   (display (format #f  "heap size does not remain constant: got ~a want ~a\n"
			    after-heap-size ideal-heap-size))
	   (set! ok #f)
	   ))

     (set! total-cells-allocated (- (cdr (assoc 'total-cells-allocated end-stats)) total-cells-allocated))

     (if (> total-cells-allocated (inexact->exact (truncate (* 1.1 tried-total-alloc))))
	 (begin
	   (display (format #f "total-cells-allocated is off: got ~a, want ~a\n"
			    total-cells-allocated tried-total-alloc))
	   (set! ok #f)
	   ))

     ok)
   #t
   (display "FAIL!!!\n")))
