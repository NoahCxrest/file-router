package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

const (
	port        = 8080
	baseURL     = "http://cdn_zipline:3000/u/"
	maxIDLength = 100
)

type fetchResult struct {
	data        []byte
	contentType string
	url         string
	err         error
}

func isValidID(id string) bool {
	if len(id) == 0 || len(id) > maxIDLength {
		return false
	}
	for _, r := range id {
		if !((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '-' || r == '_') {
			return false
		}
	}
	if strings.Contains(id, "/") || strings.Contains(id, "\\") || strings.Contains(id, "..") {
		return false
	}
	return true
}

func fetchImage(ctx context.Context, url string) fetchResult {
	client := &http.Client{Timeout: 10 * time.Second}
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return fetchResult{err: err}
	}
	resp, err := client.Do(req)
	if err != nil {
		return fetchResult{err: err}
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fetchResult{err: fmt.Errorf("status %d", resp.StatusCode)}
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return fetchResult{err: err}
	}
	contentType := resp.Header.Get("Content-Type")
	if contentType == "" {
		contentType = "application/octet-stream"
	}
	return fetchResult{data: data, contentType: contentType, url: url}
}

func fetchFirstImage(id string) fetchResult {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	webpURL := baseURL + id + ".webp"
	pngURL := baseURL + id + ".png"
	jpgURL := baseURL + id + ".jpg"
	ch := make(chan fetchResult, 3)
	go func() {
		ch <- fetchImage(ctx, webpURL)
	}()
	go func() {
		ch <- fetchImage(ctx, pngURL)
	}()
	go func() {
		ch <- fetchImage(ctx, jpgURL)
	}()
	var webpResult, pngResult, jpgResult fetchResult
	for i := 0; i < 3; i++ {
		result := <-ch
		if strings.HasSuffix(result.url, ".webp") {
			webpResult = result
		} else if strings.HasSuffix(result.url, ".png") {
			pngResult = result
		} else {
			jpgResult = result
		}
	}
	if webpResult.err == nil && strings.HasPrefix(webpResult.contentType, "image/") {
		return webpResult
	}
	if pngResult.err == nil && strings.HasPrefix(pngResult.contentType, "image/") {
		return pngResult
	}
	if jpgResult.err == nil && strings.HasPrefix(jpgResult.contentType, "image/") {
		return jpgResult
	}
	return fetchResult{err: fmt.Errorf("no valid image found")}
}

func handler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	if r.Method == "OPTIONS" {
		w.Header().Set("Access-Control-Allow-Methods", "GET")
		w.Header().Set("Access-Control-Allow-Headers", "*")
		w.WriteHeader(http.StatusOK)
		return
	}
	if r.Method != "GET" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	id := r.URL.Path[1:]
	if !isValidID(id) {
		http.Error(w, "Invalid ID", http.StatusBadRequest)
		return
	}
	fmt.Printf("Request for image ID: %s\n", id)
	result := fetchFirstImage(id)
	if result.err != nil {
		fmt.Printf("Image not found for ID: %s\n", id)
		http.Error(w, "Image not found", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", result.contentType)
	w.Header().Set("Cache-Control", "public, max-age=3600")
	w.WriteHeader(http.StatusOK)
	io.Copy(w, bytes.NewReader(result.data))
	fmt.Printf("Served %s for ID: %s\n", result.contentType, id)
}

func main() {
	http.HandleFunc("/", handler)
	fmt.Printf("Server running on port %d\n", port)
	http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
}